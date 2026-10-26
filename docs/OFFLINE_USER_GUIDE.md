# VibeStudio Offline User Guide (0.1.0-alpha.1)

The VibeStudio user manual in a single file, for reading offline. It is
generated from the pages in [docs/manual](manual/index.md) by
`scripts/generate_offline_guide.py`; edit those pages, not this file.
Release packages also include the manual as HTML: open
`docs/html/index.html` in a browser.

> [!WARNING]
> VibeStudio is pre-alpha and highly untested. Work on copies of your files
> and keep backups. [Project status](manual/status.md) explains what is and
> is not tested.

## Contents

- **Get started**
  - [Welcome to VibeStudio](#welcome-to-vibestudio)
  - [Project status](#project-status)
  - [Install VibeStudio](#install-vibestudio)
  - [First-run setup](#first-run-setup)
  - [A tour of the studio](#a-tour-of-the-studio)
- **Use the studio**
  - [Projects and game installations](#projects-and-game-installations)
  - [Level editing](#level-editing)
  - [Editor profiles and controls](#editor-profiles-and-controls)
  - [Packages](#packages)
  - [Package and release](#package-and-release)
  - [Textures and sprites](#textures-and-sprites)
  - [Materials and shaders](#materials-and-shaders)
  - [Models](#models)
  - [Audio](#audio)
  - [Code, scripts and shaders](#code-scripts-and-shaders)
  - [Build and launch](#build-and-launch)
  - [AI assistant (optional)](#ai-assistant-optional)
- **Reference**
  - [Accessibility and languages](#accessibility-and-languages)
  - [Command line](#command-line)
  - [Troubleshooting and FAQ](#troubleshooting-and-faq)
  - [Build from source](#build-from-source)

## Welcome to VibeStudio

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
> them in VibeStudio. [Project status](manual/status.md) explains what is and is not
> tested.

### What you can do today

**Available** means implemented and covered by automated tests, but not yet
proven in real projects. **Partial** means some formats or workflows work; the
linked page lists the gaps. **Planned** means not built yet.

| Area | What works today | Status |
| --- | --- | --- |
| [Workspace and projects](manual/projects.md) | Open a project folder and write its manifest, check project health, find files by name, add or detect game installations, save and reopen workspaces | Available |
| [Level editing: Doom family](manual/levels.md) | Open, edit and save Doom and Hexen maps in WADs, including UDMF; draw and shape sectors; build nodes with ZDBSP or ZokumBSP | Partial |
| [Level editing: Quake family](manual/levels.md) | Edit Quake and Quake II `.map` brushes, entities and texture alignment in a four-view workspace; prefabs; compile with VibeMap2 | Partial |
| [Level editing: Quake III](manual/levels.md) | Brushes, curved patches and shader images in the 3D view; compile with VibeMap3 and package the result as a PK3 | Partial |
| [Familiar editor controls](manual/editor-profiles.md) | 19 editor profiles bring the keys, mouse gestures, camera and layout of editors such as TrenchBroom, NetRadiant Custom, GtkRadiant, Hammer and Ultimate Doom Builder | Partial |
| [Models](manual/models.md) | View and animate MDL, MD2 and MD3 models; edit meshes, including imported OBJ files, and export MDL, MD2 or MD3; design simple props; assemble tagged models; build Quake III player packages | Partial |
| [Textures](manual/textures.md) | Browse images, sprites and palettes; paint in the layered Texture Editor; export PNG, TGA, PCX, Quake WAD2, Quake II WAL and Doom flats and patches | Partial |
| [Audio](manual/audio.md) | Preview sounds in packages; edit WAV, MP3, FLAC, Ogg Vorbis and Doom sounds; multitrack sessions; deliver sounds in each game's format. Linux builds cannot play or record audio yet | Partial |
| [Packages](manual/packages.md) | Browse, extract, validate and compare PAK, WAD, ZIP and PK3 files and plain folders; stage changes with undo; save a new package; drafts and automatic recovery | Available |
| [Package and release](manual/releases.md) | Package a project, map, model or textures with only your own files, leaving out what the game already has; keep a changelog; generate release notes and a readme; publish with a distribution archive and a record of each release | Partial |
| [Code and scripts](manual/code.md) | Edit QuakeC, shader scripts, configs and other text with highlighting, project-wide search and replace, and optional local language servers | Partial |
| [Materials and shaders](manual/materials.md) | Every texture, Quake III shader and Doom 3 material of idTech 1 to 4: a live, animated preview drawn by each engine's rules, editing as text or as nodes, checks the game would make, and saving into the package | Partial |
| [Build and launch](manual/build-and-launch.md) | Run chained compile pipelines with VibeMap2, VibeMap3, ZDBSP or ZokumBSP, jump to problems and leaks, and launch the game with your map. You provide the compiler programs | Partial |
| [AI assistant (optional)](manual/ai.md) | Off by default (AI-free mode). Opt in to ask a model about your map, code or build in the Assistant, and to use AI when generating levels, textures and sounds | Partial |
| [Command line](manual/cli.md) | Scriptable commands for projects, packages, maps, assets, builds and settings, with JSON output and stable exit codes; `cli commands` lists them all | Available |
| [Accessibility and languages](manual/accessibility.md) | High-contrast themes, text up to 200%, colour-vision options, every command reachable from the keyboard, screen reader announcements and spoken status. 47 interface languages are registered, but no translation is finished yet | Partial |

Not built yet: a sprite animation editor, and agentic AI workflows that carry
out multi-step work for you. The
[roadmap](ROADMAP.md) tracks them.

### Start here

1. [Install VibeStudio](manual/install.md): download, check and install a release, or
   run it from a folder.
2. [First-run setup](manual/first-run.md): choose your language, accessibility
   options, editor profile, games and compilers.
3. [A tour of the studio](manual/tour.md): find your way around the window, the pages
   and the panels.
4. [Project status](manual/status.md): what is tested, what is not, and how VibeStudio
   protects your files.

### Get help

- [Troubleshooting](manual/troubleshooting.md) covers common problems and where to
  look when something goes wrong.
- Report bugs and request features on
  [GitHub issues](https://github.com/themuffinator/VibeStudio/issues).
  [Report a problem](manual/status.md#report-a-problem) explains what to include.

## Project status

VibeStudio is pre-alpha. This page explains what that means, what has and has
not been tested, how VibeStudio protects your files, and the known limitations
in each area.

> [!WARNING]
> Do not use VibeStudio for production work yet. Open copies of your projects,
> maps and packages, and keep your own backups.

### What pre-alpha means here

VibeStudio is still being built. New features arrive often, and existing ones
change. Several parts of the planned studio, such as a sprite animation
editor and agentic AI workflows, do not exist yet. The parts that do exist have
automated tests, but almost none of them have been used by real people on real
projects. Expect rough edges, missing pieces and bugs, and please report what
you find.

### What is tested

Every pull request and every change to the main branch is built and tested on
Windows, macOS and Linux by the project's continuous integration (the **pr-ci**
workflow on GitHub Actions). Each run:

- builds VibeStudio and runs several hundred automated tests: format readers
  and writers, parser fuzzing and deliberately damaged files, saving and
  recovery, command-line commands, theme contrast checks, and GUI tests that
  drive the real studio window with simulated input on an off-screen display;
- checks what the 3D views draw on Windows and Linux, with Mesa's software
  Vulkan driver, since the runners have no graphics card. The macOS runners
  have no graphics driver the tests can use, so those drawing checks are
  skipped there;
- starts the studio in a self-test mode that builds and paints every page;
- checks that the command-line documentation matches the commands the program
  really has;
- validates the sample projects, the documentation links, the credits and
  licence notices, the translation catalogues and the layout of the release
  packages.

### What is not tested yet

- **Real projects at scale.** Large maps, big packages and long editing
  sessions have not been measured.
- **Fresh installs.** The Windows installer and portable zip, the macOS disk
  image and the Linux AppImage have not been tried on clean machines.
- **Hands-on use on macOS and Linux.** Automated tests run there, but the
  builds have not yet been tested by hand.
- **Assistive technology.** Screen reader support has not been checked with
  real screen readers and their users.
- **Translations.** Apart from English, none of the 47 interface languages
  has a finished, reviewed translation.
- **The games themselves.** Tests check the files VibeStudio writes, but few of
  those files have been loaded in every game and source port they target.
- **Hard failures.** Safe package saving has not been verified after a power
  loss or on network drives.
- **Graphics drivers.** The OpenGL and Vulkan renderers have been tried on a
  few Windows drivers (NVIDIA and Intel) and with Mesa's software Vulkan
  driver in CI; other graphics cards, macOS and Linux desktop drivers have not
  been tried.

### Keep your work safe

VibeStudio is built to avoid destroying your files. These protections help,
but they do not replace your own backups. On macOS, press Cmd where this page
says Ctrl.

- **Maps.** **Save Map** (<kbd>Ctrl</kbd>+<kbd>S</kbd> while Levels has focus)
  writes back to the map's file only after checking that nothing else changed
  it since you opened it, and first copies the previous version into a
  `.vibestudio/map-backups` folder beside the map. If the file changed outside
  VibeStudio, use **Save Map As…** instead. A map opened from inside a package
  always goes through Save As, and the package itself is not changed. While
  you edit, a recovery checkpoint is kept every minute; **File** >
  **Recover Maps…** restores one as unsaved work.
- **Packages.** Adding, replacing, renaming and deleting files only stages
  changes. Nothing is written until you choose **Save Package As…** and a
  destination. Writing over an existing file asks **Replace Existing Package?**
  first; the new archive is written and verified beside it, and the original
  is kept with `.bak` added to its name. Checkpoints are kept every 30 seconds
  by default, and **File** > **Recover Packages…** restores them.
- **Game installations.** Detecting games only reads. Installation profiles
  are read-only by default, and removing a profile never touches game files.
  VibeStudio writes into a game installation only when you allow it: to test a
  build it copies the compiled map into the game's `maps` folder after you
  choose **Allow Test Maps** for that installation (clear **Allow test maps**
  on the Workspace page to take that back), and **Deploy Prepared Build**
  writes a reviewed package into the game folder only after you confirm it,
  keeping a backup of any package it replaces.
- **Releases.** Publishing writes only into the release's output folder and
  the project's changelog and `.vibestudio/releases` record, never into the
  game. Publishing the same version again is refused unless you choose to
  replace it, and replaced files are kept with `.bak` added to their names.
  Indexing a game's assets only reads its packages.
- **Everything else.** Commands that write files report the exact output path
  in the **Activity** panel, and many command-line commands that write files
  offer `--dry-run` to show what would happen first.

> [!TIP]
> Keep your projects under version control, or make a dated copy before you
> try a workflow for the first time.

### Known limitations

These are the gaps you are most likely to meet. The
[support matrix](SUPPORT_MATRIX.md) lists every format and workflow with its
exact limits.

| Area | Main limitations |
| --- | --- |
| Everywhere | 3D views and material previews need OpenGL 3.3 or Vulkan 1.0; without either they stay empty and say why. Performance on large maps and packages has not been measured. |
| Level editing | The 3D view shows textures, but not Quake III shader effects such as blending, animation and deformation, nor Doom sector lighting and skies. Doom maps need an external node builder after geometry edits. |
| Editor profiles | Profiles reproduce keys, mouse gestures, camera and layout, not every behaviour of the original editors, and they do not add other engines' formats. |
| Models | Formats from Doom source ports to the Doom 3 family are read, but only tested against files the tests build, not real game files. Skeletal models are posed into frames; joints and weights cannot be edited yet. MD5, IQM and ASE exports have not been loaded in the original games. |
| Textures | No sprite animation editing or pressure-sensitive painting. A canvas holds at most 4,194,304 pixels (2048 × 2048). |
| Audio | Linux builds cannot play or record audio. |
| Packages | Renaming or deleting files does not update references to them in scripts, shaders or metadata. |
| Releases | A release finds what maps, models and shader scripts name, and what QuakeC, Quake III menus and configs, and DECORATE, ZScript, SNDINFO and MAPINFO name in full. Files that compiled game code loads, or whose names a script builds while the game runs, must be added with include patterns. Telling the game's files from yours needs the game indexed first. Not yet tried with real game installations. |
| Code | Language-server support is an early client that some servers will not work with. |
| Materials and shaders | The preview is an approximation on a single shape, without map lightmaps or Doom 3 fragment programs beyond the light interaction; videos show a placeholder. Return to Castle Wolfenstein, Enemy Territory and Jedi Knight shader keywords are read with a warning, but only Enemy Territory's implicit images are drawn. Not yet tried on real game packages. |
| Build and launch | VibeStudio does not include the compiler programs; you point it at your own. Its own VibeMap2 and VibeMap3 compilers have not yet been tested end to end inside VibeStudio. |
| First-run setup | A checklist that opens the right settings; role presets, guided project creation and a toolchain check are not part of it yet. |
| AI | Agentic workflows are not built. Cloud connectors need your own API keys. |
| Languages and accessibility | No finished translations. Not yet tested with real screen readers. |

<details>
<summary>More limitations by area</summary>

- **Level editing:** prefabs work only within the same Quake-family game and
  map format, and there are no Doom prefabs. Maps are limited to 512 MiB, and
  UDMF map text to 64 MiB. Exporting a Doom map's assets as a subset package is
  disabled.
- **Models:** collision boxes are not stored in the native model formats.
  Quake III player packages support non-team models only, without custom
  sounds or levels of detail.
- **Textures:** no sprite or Half-Life WAD3 encoding, and no Doom wall-texture
  composition. DDS export is uncompressed; DDS cube maps, volumes and arrays
  are not supported.
- **Audio:** session meters do not show live LUFS or true-peak readings. Tempo
  ramps, musical anchoring and automation recording are not built. Recording
  has not been tested across a range of audio hardware.
- **Packages:** opening a package is limited to 250,000 entries.
- **Code:** workspace-wide diagnostics and edits started by a language server
  are not supported.
- **Setup:** the CLI Integration step only describes the command line, and
  there is no guided keyboard-only walkthrough yet.

</details>

### Platform notes

- **Builds are not signed.** Windows SmartScreen may show **Windows protected
  your PC**, and macOS Gatekeeper blocks the first launch.
  [Install VibeStudio](manual/install.md) shows how to open them safely.
- **macOS:** builds run on Macs with Apple silicon and macOS 13 or later. There
  is no Intel build.
- **Linux:** AppImage builds do not include in-studio audio playback or
  recording. They need FUSE 2 and glibc 2.35 or newer.
- **Windows:** builds run on 64-bit Windows 10 and 11.

### Report a problem

1. Check [Troubleshooting](manual/troubleshooting.md) for a known fix.
2. Open a [GitHub issue](https://github.com/themuffinator/VibeStudio/issues)
   with the **Bug report** template. Say what you did, what you expected and
   what happened, and include your VibeStudio version (**Help** >
   **About VibeStudio**) and operating system.
3. Attach diagnostics:
   - In the studio, **Tools** > **Copy Diagnostic Bundle**
     (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>D</kbd> on Windows and Linux) copies
     a summary to the clipboard: version, platform, the 3D renderer, the open
     project and package, the compilers found, the number of crash reports, and
     the latest lines of the session log. Paste it into the issue. It contains folder
     paths, so remove any you do not want to share.
   - From the command line, the command below writes
     `vibestudio-diagnostics.json` into the folder you name. It leaves out
     secrets, API keys, environment values, home-folder contents and project
     files.

     ```sh
     vibestudio --cli diagnostics bundle --output ./diagnostics
     ```

   - After a crash, **Help** > **Crash Reports…** shows the reports kept on
     your computer, and **Copy Report** copies one.
     `vibestudio --cli diagnostics crashes` lists them with their paths.

### Learn more

- [Roadmap](ROADMAP.md): milestones and what comes next.
- [Support matrix](SUPPORT_MATRIX.md): every format and workflow, with its
  exact limits.

## Install VibeStudio

VibeStudio runs on Windows, macOS and Linux. This page shows you how to
download a release, check it, install it, run its command line, and remove it
again.

> [!IMPORTANT]
> Every build is pre-alpha, and builds are not signed yet. Download VibeStudio
> only from the project's
> [Releases page](https://github.com/themuffinator/VibeStudio/releases), and
> check each file against `SHA256SUMS.txt` before you run it.

### Download a release

Releases live on the
[VibeStudio Releases page](https://github.com/themuffinator/VibeStudio/releases).
Every release lists its files, a `SHA256SUMS.txt` checksum file and release
notes. Pre-release versions are marked **Pre-release**. In the file names
below, `<version>` is the release's version number.

| System | Download | What it is |
| --- | --- | --- |
| Windows 10 or 11, x64 | `VibeStudio-<version>-windows-x64-setup.exe` | Installer |
| Windows 10 or 11, x64 | `VibeStudio-<version>-windows-x64-portable.zip` | Portable copy that runs from any folder |
| macOS 13 or later, Apple silicon | `VibeStudio-<version>-macos-arm64.dmg` | Disk image |
| Linux x86_64 | `VibeStudio-<version>-linux-x86_64.AppImage` | Single runnable file |
| Any | `VibeStudio-<version>-docs.zip` | Offline HTML documentation; every package also contains it |
| Any | `VibeStudio-<version>-source.tar.gz` | Full source code, including the compiler submodules |

VibeStudio's 3D views (the Levels camera, models, the modeller and material
previews) need a graphics driver with OpenGL 3.3 or Vulkan 1.0, which almost
every computer from the last decade has. Without one, everything else works and
the 3D views say why they are empty; see
[3D views stay empty](manual/troubleshooting.md#3d-views-stay-empty).

### Verify your download

Put `SHA256SUMS.txt` in the same folder as the files you downloaded, open a
terminal in that folder, and run the command for your system. Each file you
downloaded should report `OK`.

On Linux:

```sh
sha256sum -c SHA256SUMS.txt --ignore-missing
```

On macOS:

```sh
shasum -a 256 -c SHA256SUMS.txt --ignore-missing
```

On Windows, in PowerShell, print the file's hash and compare it with that
file's line in `SHA256SUMS.txt`. Letter case does not matter.

```powershell
Get-FileHash .\VibeStudio-<version>-windows-x64-setup.exe -Algorithm SHA256
```

### Install on Windows

#### Use the installer

1. Run `VibeStudio-<version>-windows-x64-setup.exe`.
2. If Windows SmartScreen says **Windows protected your PC**, select
   **More info**, then **Run anyway**. SmartScreen warns because the installer
   is not signed yet.
3. Choose who to install for. By default VibeStudio installs for your account
   only and needs no administrator rights; you can also install it for all
   users.
4. Choose whether to add a desktop shortcut, then finish the installer.
5. Start **VibeStudio** from the Start menu. The Start menu also has
   **VibeStudio Documentation**, which opens this manual offline.

#### Use the portable zip

1. Extract `VibeStudio-<version>-windows-x64-portable.zip` to any folder you
   can write to.
2. Run `bin\vibestudio.exe` inside that folder.
3. If SmartScreen appears, select **More info**, then **Run anyway**.

A portable copy keeps your preferences in the same place as an installed one.
To keep them beside the program instead, see
[Use a settings file of your own](manual/install.md#use-a-settings-file-of-your-own).

### Install on macOS

You need a Mac with Apple silicon and macOS 13 or later.

1. Open `VibeStudio-<version>-macos-arm64.dmg`.
2. Drag **VibeStudio** into **Applications**.
3. Open VibeStudio from **Applications**. Because the app is not signed or
   notarised yet, macOS blocks this first launch.
4. Open **System Settings** > **Privacy & Security**, find the message about
   VibeStudio, and choose **Open Anyway**. Confirm, and enter your password if
   asked. From then on VibeStudio opens normally.

On macOS 13 and 14 you can instead Control-click VibeStudio in
**Applications**, choose **Open**, then confirm with **Open**.

If you prefer the Terminal, remove the quarantine flag instead. Only do this
for a copy you downloaded from the Releases page and verified.

```sh
xattr -dr com.apple.quarantine /Applications/VibeStudio.app
```

### Install on Linux

The AppImage runs on x86_64 Linux with glibc 2.35 or newer, such as Ubuntu
22.04 or later; it is built on Ubuntu 22.04.

1. Make the file executable:

   ```sh
   chmod +x VibeStudio-<version>-linux-x86_64.AppImage
   ```

2. Run it:

   ```sh
   ./VibeStudio-<version>-linux-x86_64.AppImage
   ```

If it stops with a message about FUSE, install FUSE 2: the package is
`libfuse2` on Ubuntu 22.04 and `libfuse2t64` on Ubuntu 24.04 and later. You can
also run the AppImage without FUSE:

```sh
./VibeStudio-<version>-linux-x86_64.AppImage --appimage-extract-and-run
```

> [!NOTE]
> Linux builds do not include in-studio audio playback or recording yet. You
> can still open, edit and export sounds.

### Run the command line

The same program is also the command-line tool: add `--cli` to run a command
instead of opening the window.

| System | Command |
| --- | --- |
| Windows | `vibestudio.exe --cli --help`, run from the `bin` folder where you installed or extracted VibeStudio |
| macOS | `/Applications/VibeStudio.app/Contents/MacOS/vibestudio --cli --help` |
| Linux | `./VibeStudio-<version>-linux-x86_64.AppImage --cli --help` |

In PowerShell, start the command with `.\` when you run it from the `bin`
folder:

```powershell
.\vibestudio.exe --cli --help
```

This manual writes commands as `vibestudio --cli <family> <command>`; replace
`vibestudio` with the command for your system. [Command line](manual/cli.md) explains
the commands, JSON output and exit codes.

### Try a nightly build

Nightly builds come from the **release-nightly** workflow on GitHub Actions.
They have the newest changes and even less testing than releases.

1. Open the
   [release-nightly workflow](https://github.com/themuffinator/VibeStudio/actions/workflows/release-nightly.yml)
   and select a successful run.
2. Download the artifact for your system from the run's **Artifacts** list.
   You need to be signed in to a GitHub account to download artifacts.

To build VibeStudio yourself, see [Building from source](manual/building-from-source.md).

### Where VibeStudio keeps your settings

VibeStudio stores its preferences with Qt's settings system, under the
organisation name `DarkMatterProductions` and the application name
`VibeStudio`. Session logs, crash reports and recovery copies live in your
per-user application data folders.

| What | Windows | macOS | Linux |
| --- | --- | --- | --- |
| Preferences | Registry key `HKEY_CURRENT_USER\Software\DarkMatterProductions\VibeStudio` | A `.plist` file in `~/Library/Preferences` | `~/.config/DarkMatterProductions/VibeStudio.conf` |
| Session logs and crash reports | `%APPDATA%\DarkMatterProductions\VibeStudio\logs` | `~/Library/Application Support/DarkMatterProductions/VibeStudio/logs` | `~/.local/share/DarkMatterProductions/VibeStudio/logs` |
| Recovery copies of unsaved work | `%LOCALAPPDATA%\DarkMatterProductions\VibeStudio` | `~/Library/Application Support/DarkMatterProductions/VibeStudio` | `~/.local/share/DarkMatterProductions/VibeStudio` |

Each day's session log is a separate `vibestudio-<date>.log` file. To print
exactly where your preferences are stored, run:

```sh
vibestudio --cli --settings-report
```

#### Use a settings file of your own

Start VibeStudio with `--settings-file <path>` to keep preferences in an INI
file instead, for example for a portable copy or a separate test profile.
Recovery copies and crash reports then go in the same folder as that file;
session logs still go to the logs folder above. Use a full path, and pass the
same option every time you start VibeStudio.

```powershell
.\bin\vibestudio.exe --settings-file "D:\VibeStudio\settings.ini"
```

The option works for the command line too, so scripts can run without touching
your own preferences.

### Uninstall

- **Windows installer:** open **Settings** > **Apps** > **Installed apps**,
  find **VibeStudio**, and choose **Uninstall**. On Windows 10 the list is
  under **Settings** > **Apps** > **Apps & features**.
- **Windows portable zip:** delete the folder you extracted.
- **macOS:** quit VibeStudio and move it from **Applications** to the Bin.
- **Linux:** delete the AppImage file.

For a clean slate, also delete the preferences, logs and recovery folders
listed in [Where VibeStudio keeps your settings](manual/install.md#where-vibestudio-keeps-your-settings).
Recovery copies can hold unsaved work, so check **File** > **Recover Maps…**
and **File** > **Recover Packages…** first.

## First-run setup

The first-run setup is an eight-step checklist that tailors VibeStudio to you:
language and accessibility, your editor profile, projects, game installations,
compilers, AI and the command line. Every step is optional, and you can change
every choice later.

> [!NOTE]
> **Status: Partial.** The checklist tracks your progress and opens the right
> settings for each step. Some planned parts of the flow are not built yet:
> role presets, guided project creation, a toolchain check inside setup, and
> command-line path setup.

On macOS, press Cmd wherever this page says Ctrl.

### Open the checklist

VibeStudio does not open the setup by itself: it starts on the **Workspace**
page with its defaults. To begin:

1. Open **Settings**: select it at the foot of the left rail, or press
   <kbd>Ctrl</kbd>+<kbd>0</kbd>.
2. Select **Getting Started**, the first category.

The panel shows the setup status (**Not Started**, **In Progress**,
**Skipped For Now** or **Complete**), the current step, a progress bar, and the
eight steps, each marked done, current or pending. Below the steps it lists
reminders such as "No game installation profile has been added yet."

### Work through the steps

1. Select **Start**. The first step, **Welcome and Access**, becomes the current
   step.
2. Select the step's settings button. Its label says where it goes, for
   example **Open Accessibility Settings**.
3. Make your choices. Settings apply immediately and are saved automatically.
4. Go back to **Settings** > **Getting Started** and select **Next**.
5. Repeat for each step. **Next** on the last step marks setup complete, and
   **Finish** does so at any point after you start.

| Step | What you choose | Settings button |
| --- | --- | --- |
| Welcome and Access | Language, region formats, theme, text size, colour vision, focus outline, motion, alerts and speech | **Open Accessibility Settings** |
| Workspace and Editor Profile | The editor profile the Levels page follows, and the 3D renderer | **Choose Editor Profile** |
| Projects and Packages | A project folder and its manifest, or nothing for now | **Open Workspace** |
| Game Installations | Your games, detected from Steam and GOG or added by hand | **Open Workspace** |
| Toolchains | Where your compiler programs are | **Open Build Toolchains** |
| AI and Automation | Whether to stay in AI-free mode | **Open AI Settings** |
| CLI Integration | Nothing to set; the command line is part of the same program | None |
| Review and Finish | Check the reminders, then finish | None |

### Language, appearance and accessibility

Language and appearance are on **Settings** > **Appearance and Language**:

- **Language**: the default, **System language**, follows your operating
  system whenever VibeStudio has that language. Choosing another offers
  **Restart Now**, and the new language appears after the restart. 47 languages
  are listed, but none is fully translated yet, so most of the interface stays
  in English.
- **Region formats**: how numbers, dates and sizes are written, chosen apart
  from the language, with a live sample.
- **Theme**: **System**, **Dark** (the default), **Light**,
  **High Contrast Dark** or **High Contrast Light**.
- **Text scale** from 100% to 200%, **Density**, **Typeface**, and
  **Wider letter and word spacing**.
- **Navigation**: how the left rail uses its width.

**Open Accessibility Settings** opens **Settings** > **Accessibility**:

- **Vision**: **Colour vision** for status colours (standard, red-green safe,
  blue-yellow safe or monochrome), **Reduce colour saturation**,
  **Thick focus outline** and **Thick text cursor**.
- **Motion and Timing**: **Reduce motion**,
  **Stop the text cursor blinking**, and how long status messages stay.
- **Alerts and Screen Readers**: announcements to your screen reader, a
  taskbar flash, and sound cues when long tasks finish.
- **Text to Speech**: **Read status changes aloud** with your computer's own
  voices, chosen events, voice, rate, pitch and volume. **Say Test Phrase**
  checks that it works.

See [Accessibility and languages](manual/accessibility.md) for every option.

### Editor profile

**Choose Editor Profile** opens **Settings** > **Appearance and Language** at
**Editor profile**. Pick the editor whose keys, mouse and camera you already
know: the 24 profiles include TrenchBroom, NetRadiant Custom, GtkRadiant 1.4,
1.5 and 1.6, QeRadiant, Q3Radiant, DoomEdit, BSP, QuArK, Hammer and Ultimate Doom Builder.
The Levels **Controls** menu also offers **Browse Editor Profiles…**, with search
and previews of each profile’s defaults and differences.
**VibeStudio Default** is selected to
start with. **Customize Gestures…** adjusts the chosen profile. See
[Editor profiles](manual/editor-profiles.md) for the supported controls and remaining
differences; choosing a profile does not add its original editor's file formats.

The same page has **3D Rendering**. Its **Renderer** decides what draws the
Levels camera, models, the modeller and material previews:

- **Automatic** (the default) uses Vulkan where it works and OpenGL otherwise.
  On macOS it tries OpenGL first.
- **OpenGL** or **Vulkan** uses only that one.

**Status** shows what each renderer found on your computer and which one is in
use. **Check Renderers** starts both again and draws a test image on each; use
it after updating a graphics driver. See
[3D views stay empty](manual/troubleshooting.md#3d-views-stay-empty) if neither works.

### Projects and game installations

**Open Workspace** opens the **Workspace** page.

- **Open Project** (<kbd>Ctrl</kbd>+<kbd>O</kbd>) chooses a project folder.
  **Initialize Manifest** writes the project's `.vibestudio/project.json`. See
  [Projects and game installations](manual/projects.md).
- **Detect Installs** looks for games in your Steam and GOG libraries. Found
  games appear under **Game Installations**; select one and choose
  **Import Detected** to keep it.
- **Add** creates a profile from a game folder you choose. **Use** makes the
  selected profile the default, and **Remove** forgets a profile without
  touching its files.

Detection only reads, and every new profile is read-only: VibeStudio copies a
test map into a game only after you allow it. See
[Build and launch](manual/build-and-launch.md).

### Compilers

**Open Build Toolchains** opens the **Build** page. Its **Toolchain** tab lists
each compiler tool (VibeMap2 for Quake and Quake II, VibeMap3 for Quake III,
ZDBSP and ZokumBSP for Doom), whether it was found, and where its path comes
from. VibeStudio also looks on your `PATH`. VibeMap2 and VibeMap3 are
VibeStudio's own compilers; stock ericw-tools and q3map2 programs can still be
used, but you choose them with **Locate…**.

1. Select a tool whose status is **Not found**.
2. Choose **Locate…** and pick its program.
3. After you install a tool somewhere else, choose **Rescan**.

The compiler item in the status bar shows how many tools were found and opens
this tab. See [Build and launch](manual/build-and-launch.md).

### AI and automation

**Open AI Settings** opens **Settings** > **AI and Automation**. **AI-free mode**
is on by default: no AI feature runs and nothing is sent anywhere. To use the
Assistant or AI-assisted generation, clear **AI-free mode**, choose a connector
and model under **Text Model**, and select **Allow cloud AI connectors** if the
model is not on your computer or you use Claude Code, Codex or Custom Command.
See [AI assistant](manual/ai.md).

### Command line and finishing

**CLI Integration** has no settings: the command line is the same program with
`--cli` added, as [Run the command line](manual/install.md#run-the-command-line)
shows. **Review and Finish** is the moment to read the reminders under the
steps, then select **Finish**.

### Skip now, finish later

- **Skip** sets setup to **Skipped For Now**. Nothing else changes.
- **Resume** continues from the step you left.
- **Finish** marks setup complete. After that, **Review** reopens the last
  step.
- **Reset** clears setup progress only. Your settings stay as they are.

### Change your choices later

Everything setup touches is an ordinary setting:

- **Settings** (<kbd>Ctrl</kbd>+<kbd>0</kbd>) has **Getting Started**,
  **Appearance and Language**, **Accessibility**, **AI and Automation** and
  **Extensions**. Type in **Search settings** to find a setting, and press
  <kbd>Enter</kbd> to move to the first match.
- **Tools** > **Preferences** (<kbd>Ctrl</kbd>+<kbd>,</kbd>) opens
  **Appearance and Language**, and **Tools** > **Accessibility Settings** opens
  **Accessibility**.
- The Command Palette (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd>) runs
  commands such as **Larger Text**, **Toggle High Contrast** and
  **Detect Game Installations** from any page.

### Set up from the command line

The command line reads and changes the same settings, which helps when you
prepare several computers or a test profile. Setup progress and preferences
use options rather than command families:

```sh
vibestudio --cli --setup-report
vibestudio --cli --setup-step game-installations
vibestudio --cli --set-theme high-contrast-dark
vibestudio --cli --set-text-scale 150
vibestudio --cli editor select trenchbroom
vibestudio --cli render set vulkan
vibestudio --cli install detect --json
vibestudio --cli project init ./mymod
vibestudio --cli compiler set-path vibemap2-bsp --executable /opt/vibemap2/vibemap2-bsp
```

Setup progress also accepts `--setup-start`, `--setup-next`, `--setup-skip`,
`--setup-complete` and `--setup-reset`. The step IDs are `welcome-access`,
`workspace-profile`, `projects-packages`, `game-installations`, `toolchains`,
`ai-automation`, `cli-integration` and `review-finish`. Run
`vibestudio --cli --help` for every `--set-` option, and add
`--settings-file <path>` to change a separate settings file instead of your
own. See [Command line](manual/cli.md).

### Learn more

- [Initial setup flow](INITIAL_SETUP.md): the full design for setup,
  including the parts not built yet.
- [Accessibility and localisation](ACCESSIBILITY_LOCALIZATION.md): the
  accessibility and language design in depth.

## A tour of the studio

This page shows you around the VibeStudio window: the bar along the top, the
pages on the left rail, the side panels, the status bar, and the keys that move
you between them.

On macOS, press Cmd wherever this page says Ctrl, and Option wherever it says
Alt.

> [!TIP]
> If you cannot find something, press <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd>
> and type what you want to do. Every command is in that list.

### The studio bar

One row of controls runs along the top of the window. From left to right:

- The menus: **File**, **Edit**, **View**, **Project**, **Build**, **Tools**
  and **Help**. On macOS they sit in the menu bar at the top of the screen
  instead.
- **Go Back** and **Go Forward**, the arrow buttons: they return you to the
  page and place you were at before, like a web browser. Press
  <kbd>Alt</kbd>+<kbd>Left</kbd> and <kbd>Alt</kbd>+<kbd>Right</kbd>, or use
  the back and forward buttons on your mouse.
- **Open Project Folder…**, **Open Package…** and **Open Map…**, as icon
  buttons. Hold the pointer over any button to see its name and shortcut.
- **Search commands**, in the centre of the window. It opens the Command
  Palette.
- **Run Build Pipeline** (<kbd>F7</kbd>) and **Launch Game**
  (<kbd>Ctrl</kbd>+<kbd>G</kbd>), the two commands that start real work, close
  the row.

The window title names the current page and the open project, so the taskbar
shows where you are.

Choose **Help** > **Documentation** to open the bundled HTML user manual in
your browser. It works offline in release packages. If no bundled manual is
available, as in development builds, the command opens the
[online manual](https://github.com/themuffinator/VibeStudio/blob/main/docs/manual/index.md).
You can also find **Documentation** in the Command Palette.

### Find commands and files

| To | Press | Then |
| --- | --- | --- |
| Run any command | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd> or <kbd>Ctrl</kbd>+<kbd>K</kbd> | Type part of its name or category, or words from its description, and press <kbd>Enter</kbd> |
| Open a file | <kbd>Ctrl</kbd>+<kbd>P</kbd> (**File** > **Go to File…**) | Type part of a file or folder name, and press <kbd>Enter</kbd> to open it on the right page |
| Search the current page | <kbd>Ctrl</kbd>+<kbd>F</kbd> (**Edit** > **Find**) | Type in the page's filter or search field; on the **Code** page, the editor's find bar opens |
| Search the whole project | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd> (**Edit** > **Find In Project Files…**) | Search, and optionally replace, in every text file of the open project |

The Command Palette and Go to File match loosely, so you can skip letters:
`opk` finds **Open Package**, and `bwall` finds `brick_wall.tga`. A command's
description matches by whole words, such as `texture lock`. Commands that
cannot run right now stay in the list, dimmed. Go to File lists recent files,
your project's files and the entries of the open package; **File** >
**Open Recent** also lists recent files. In the code editor's find bar,
<kbd>F3</kbd> and <kbd>Shift</kbd>+<kbd>F3</kbd> step through the matches, and
**Edit** > **Replace…** adds a replace row (<kbd>Ctrl</kbd>+<kbd>H</kbd> on
Windows and Linux).

You can also drop a map, package, project folder, shader script or text file
onto the window to open it where it belongs.

### The left rail

The rail on the left switches between the studio's ten pages.

- It rests as icons. Its labels open while the pointer rests on it or keyboard
  focus is in it, and fold away when you leave.
- The pin at its foot, **Keep navigation open**, keeps the labels showing. In a
  narrow window a pinned rail still folds, to give the page room.
- The current page's icon takes the accent colour, with a short bar beside it.
- **Settings** sits at the foot of the rail, apart from the work pages.

To change how the rail behaves, open **Settings** > **Appearance and Language**
and choose **Navigation**: **Collapse to icons automatically** (the default),
**Always show labels** or **Icons only**.

### The pages

| Page | What it's for | Learn more |
| --- | --- | --- |
| **Workspace** (<kbd>Ctrl</kbd>+<kbd>1</kbd>) | Project health and problems, search, recent projects, game installations and AI proposals, plus tiles that say what each work page holds | [Projects](manual/projects.md) |
| **Levels** (<kbd>Ctrl</kbd>+<kbd>2</kbd>) | Open, edit and save Doom and Quake-family maps | [Level editing](manual/levels.md) |
| **Models** (<kbd>Ctrl</kbd>+<kbd>3</kbd>) | MDL, MD2 and MD3 models: previews, mesh editing, props and assemblies | [Models](manual/models.md) |
| **Textures** (<kbd>Ctrl</kbd>+<kbd>4</kbd>) | Textures, sprites and palettes, and the Texture Editor | [Textures](manual/textures.md) |
| **Audio** (<kbd>Ctrl</kbd>+<kbd>5</kbd>) | Sounds with waveform previews, and the Audio Editor | [Audio](manual/audio.md) |
| **Packages** (<kbd>Ctrl</kbd>+<kbd>6</kbd>) | Browse, extract, stage and rebuild PAK, WAD, ZIP and PK3 packages | [Packages](manual/packages.md) |
| **Code** (<kbd>Ctrl</kbd>+<kbd>7</kbd>) | Scripts, configs, shader scripts and QuakeC, with highlighting and project-wide search | [Code and scripts](manual/code.md) |
| **Materials** (<kbd>Ctrl</kbd>+<kbd>8</kbd>) | Textures, Quake III shaders and Doom 3 materials with a live, animated preview, edited as text or as nodes | [Materials and shaders](manual/materials.md) |
| **Build** (<kbd>Ctrl</kbd>+<kbd>9</kbd>) | Compile pipelines, problems, artifacts, compiler tools and game launching | [Build and launch](manual/build-and-launch.md) |
| **Settings** (<kbd>Ctrl</kbd>+<kbd>0</kbd>) | First-run setup, appearance, language, accessibility, AI and extensions | [First-run setup](manual/first-run.md) |

Each page's header names the page, sums up what it shows (for example the open
map's file name with its entity and brush counts) and holds the page's main
actions. A page with nothing open says what it needs and offers buttons to get
started.

### Keep editors in the studio

**Models**, **Textures** and **Audio** keep their authoring tools in tabs inside
the main window. **Browser** returns to the package's assets. Mesh editing,
prop design, assemblies, texture editing, waveform editing and multitrack
sessions each keep their open document while you switch tabs or use the rail.
Opening the same editor again selects its existing tab.

Close an editor with its tab's close button or <kbd>Ctrl</kbd>+<kbd>W</kbd>
while the editor has focus. Unsaved changes still prompt for a decision, and
cancelling keeps the tab open. Document shortcuts, such as Save and Undo, apply
to the focused editor. The project, package staging, level placement and build
services remain shared across pages. File choosers, confirmations and focused
tool settings still open as dialogs.

### The panels

Three panels open beside the page, on the side across from the rail. They
start closed, so every page gets the full width.

- **Activity** (the Activity Center) lists every task with its progress, log
  and result: opening packages, saves, builds and setup. Select a task to see
  its details. **Cancel** stops a task that can be stopped (<kbd>Esc</kbd> also
  works while the panel has focus), and **Clear Finished** tidies the list.
  Open it with the **Activity** button in the status bar or **View** >
  **Activity Panel**.
- **Inspector** shows settings, setup progress and project diagnostics, with
  the raw details. Open it with the **Inspector** button in the status bar or
  **View** > **Inspector Panel**.
- **Assistant** sends questions about your work to an AI model, with the
  context you choose. Until you opt in to AI, it explains what to turn on. Open
  it with **Tools** > **Assistant**, **View** > **Assistant Panel**, or the AI
  item in the status bar. See [AI assistant](manual/ai.md).

Drag a panel by its title bar to move it to the other side or the bottom, or
use its buttons to float or close it. Open panels return at their saved width
when space permits; long Activity rows shorten to fit. **View** > **Reset Layout**
restores the default proportions and closes the side panels.

### The status bar

The status bar reports what just happened on its left. On its right:

- The **Activity** and **Inspector** buttons open and close those panels.
- Five items name the state of your work in words as well as colour: the
  project (**No project** until you open one), the package (**No package**),
  the game installation (**No game**), the compilers (**No compilers**, or how
  many were found, such as **Compilers 7/9**), and AI (**AI-free** or
  **AI on**).

Hold the pointer over an item for details, or select it to go where you can act
on it: the project and game items open the **Workspace** page, the package item
opens **Packages**, the compilers item opens the **Build** page's
**Toolchain** tab, and the AI item opens the **Assistant**.

### Move around with the keyboard

| Action | Keys |
| --- | --- |
| Command Palette | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd> or <kbd>Ctrl</kbd>+<kbd>K</kbd> |
| Go to File | <kbd>Ctrl</kbd>+<kbd>P</kbd> |
| Find on the current page | <kbd>Ctrl</kbd>+<kbd>F</kbd> |
| Switch to a page | <kbd>Ctrl</kbd>+<kbd>1</kbd> to <kbd>Ctrl</kbd>+<kbd>9</kbd>, and <kbd>Ctrl</kbd>+<kbd>0</kbd> for Settings |
| Go back or forward | <kbd>Alt</kbd>+<kbd>Left</kbd>, <kbd>Alt</kbd>+<kbd>Right</kbd> |
| Preferences (**Appearance and Language**) | <kbd>Ctrl</kbd>+<kbd>,</kbd> |
| Run Build Pipeline | <kbd>F7</kbd> |
| Build and Launch | <kbd>F5</kbd> |
| Next or previous build problem | <kbd>F4</kbd>, <kbd>Shift</kbd>+<kbd>F4</kbd> |
| Read aloud, or stop reading | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>U</kbd> |
| About VibeStudio | <kbd>Shift</kbd>+<kbd>F1</kbd> |

Press <kbd>Tab</kbd> and <kbd>Shift</kbd>+<kbd>Tab</kbd> to move between
controls. Two places use Tab themselves. In the code editor Tab indents; on
Windows and Linux, <kbd>Ctrl</kbd>+<kbd>Tab</kbd> leaves the editor. In the
Levels views Tab selects the next object, and <kbd>Esc</kbd> cancels a drag,
then clears the selection, then leaves the view.

**Help** > **Keyboard Shortcuts** lists every command with its keys and the
page they work on. **Change Keys…** gives a command keys of your own, and
**Reset** puts the default back.

### Pick up where you left off

- **Reopen the last session at start** is on by default (**Settings** >
  **Appearance and Language** > **Startup and Recovery**). VibeStudio reopens
  the package, map and code files that were open, on the page you closed it on,
  and the status bar says what it reopened. Opening a file from the command
  line with `--open <path>` skips this.
- If VibeStudio closed unexpectedly, the next start shows
  **The studio closed unexpectedly last time.** in a bar above the page. The
  files that were open are not reopened by themselves, in case one of them
  caused the crash: choose **Reopen Last Session** to open them, or
  **View Report** to read the crash report. Crash reports stay on your computer
  and are listed in **Help** > **Crash Reports…**.
- Unsaved work is checkpointed in the background. **File** >
  **Recover Maps…**, **Recover Packages…**, **Recover Audio…** and
  **Recover Text Documents…** restore it.
- To save your open project, package, map and code tabs to come back to later,
  choose **File** > **Save Workspace As…**; **File** > **Open Workspace…**
  restores them.

### Learn more

- [Visual showcase](showcase/index.html): twelve development screenshots
  spanning authoring, project tools, builds and accessibility.
- [UX design](UX_DESIGN.md): the principles behind the window's layout.
- [Portable workspaces](WORKSPACES.md): what a `.vibeworkspace` file keeps.

## Projects and game installations

A project is a folder that holds your mod or map sources. This page shows how to open one, describe it
with a project manifest, tell VibeStudio where your games are installed, search and replace text
across the project, and save your place as a workspace file.

> [!NOTE]
> **Status: Available.** Project folders, manifests, game installation profiles, Steam and GOG
> detection, project search and replace, and workspace files work and have automated tests, but have
> not been proven in real projects. Detection only looks in common Steam and GOG folders, so games
> from other stores need a profile added by hand.

### The Workspace page

The Workspace page is the studio's start page. Choose **Workspace** on the left rail or press
<kbd>Ctrl</kbd>+<kbd>1</kbd>. On macOS, press <kbd>Cmd</kbd> wherever this page says <kbd>Ctrl</kbd>.

- The page header holds **Open Project**, **Initialize Manifest** and **Detect Installs**.
- A tile for each work page, from **Levels** to **Build**, says what that page holds now, such as the
  open map with its entity and brush counts. Select a tile to go to its page.
- **Project Health** collects the open project's state in five tabs (see the table below).
- **Recent Projects** and **Game Installations** list the folders and games VibeStudio remembers.
- **Releases** shows the project's unreleased changes and latest releases, with **Record Change…** and
  **Package and Release…**. See [Package and release](manual/releases.md).
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

### Open a project

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

### Describe the project with a manifest

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
| `release` | How the project is packaged and described when released; see [Release settings](manual/releases.md#release-settings). |
| `createdUtc`, `updatedUtc` | When the manifest was created and last saved. |

Keys VibeStudio does not know, for example from a newer version, are kept when it saves the manifest.

</details>

**File** > **Copy Project Manifest** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>M</kbd>) copies the resolved
manifest, for example to attach to a bug report.

### Check project health

Choose **Project** > **Validate Project** to run the health checks. VibeStudio switches to the
Workspace page and lists anything that needs attention under **Problems**: a missing manifest or
source folder, a newer manifest version, no linked game installation, or a game whose assets are not
indexed or whose index is out of date. Activate the asset index problem to index the game. Output
and temporary folders that do not exist yet are not problems: they are created when first needed.
Warnings alone do not block your work. `vibestudio --cli project validate` exits with code 4 only for blocking problems, and with code 3
when the folder has no manifest yet.

### Add your game installations

A game installation profile tells VibeStudio where a game is installed, which game it is, and which
packages it loads. Profiles tell the Build page which game to launch, and a Doom installation's base
packages give your PWADs their palette. VibeStudio never writes into a game folder without your
permission.

#### Detect Steam and GOG games

1. Choose **Detect Installs** on the Workspace header, or **Project** > **Detect Game Installations**
   (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>G</kbd>).
   Candidates appear under **Detected candidates** in **Game Installations**, with their store and a
   match percentage.
2. Select a candidate and choose **Import Detected**.
   The candidate is saved as a read-only profile.

Detection looks in common Steam library folders, including extra libraries listed by Steam, and in
common GOG folders. It recognises a game by its folder name, its program and its data files, such as
`id1/pak0.pak` for Quake. It looks for the program in the game folder and up to two folders below
it, such as `bin/x64`; for a program kept deeper, add the installation with
`install add <root> --install-executable <path>`. Detection only reads; nothing is saved until you
import a candidate.

#### Add an installation by hand

1. In **Game Installations**, choose **Add**.
2. Choose the game's root folder.
3. Choose the game from the list, for example **Quake [quake]** or **Doom-family [doom]**. Choose
   **Custom / Unknown [custom]** for anything else.
4. Enter a name, or keep the suggested one.
   The profile is saved as read-only.

#### Choose the installation to use

Select a profile and choose **Use** to make it the default; its row shows **in use**, and the Build
page launches it. Each row also shows **Ready**, or **Needs Review** when a check fails, such as a
missing root folder. **Remove** deletes the profile only, never game files. A project manifest can
also record an installation for the project, which the project health checks report.

#### Index the game's assets

Select a profile and choose **Index Assets**, or **Project** > **Index Game Assets**. VibeStudio reads
the game's own packages once, in the background, and remembers every file they hold, the Quake III
shaders they declare and the Doom-family names they define. Releases use this index to leave the
game's own files out, and the Levels page's **Dependencies** uses it to mark references the game
provides. The index is kept with VibeStudio's own data, never in the game folder.

Each row says **assets indexed**, **assets not indexed** or **asset index out of date**; an index
goes out of date when the game's packages change, for example after a patch. Indexing knows the
standard packages of Quake, Quake II, Quake III Arena, Doom, Heretic and Hexen and their official
expansions, plus any base packages saved in the profile. See [Package and release](manual/releases.md).

#### Allow test maps

Profiles are read-only to start with. Quake-family engines only load maps from a game folder's `maps`
folder, so launching a build from the Build page needs to copy the map there. The first time, the
Build page asks whether to **Allow Test Maps** for that installation; if you allow it, the profile
keeps that permission. The **Allow test maps** check box in **Game Installations** grants or
withdraws the same permission for the selected profile, and rows with permission show
**test maps allowed**. Only the built map and its lighting files are copied. See
[Build and launch](manual/build-and-launch.md).

### Search and replace across the project

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
and `external`. See [Code and scripts](manual/code.md) for the editor itself.

### Save and restore a workspace

A workspace file remembers what you were working on: the project, the open package or package draft,
the map, the saved Code tabs, the selected texture, model and sound, and the page you were on.

- Choose **File** > **Save Workspace As…** to write a `.vibeworkspace` file.
- Choose **File** > **Open Workspace…**, drop the file on the window, or start VibeStudio with
  `--open <file>` to restore it.

A workspace stores references, not contents: save your maps, packages and other documents first.
It does not keep unsaved edits, undo history or camera positions, and opening one never runs a
compiler, launches a game or installs anything. Paths are stored relative to the workspace file, so
you can move a folder that contains both.

### Command-line equivalents

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
[Command line](manual/cli.md) for running these commands.

### Learn more

- [Package and release](manual/releases.md): package the project for players.
- [Game installations](GAME_INSTALLATIONS.md): profile data, detection sources, the asset index and
  safety rules.
- [Project search](PROJECT_SEARCH.md): matching rules, limits and partial-write reporting.
- [Portable workspaces](WORKSPACES.md): the `.vibeworkspace` format.
- [CLI strategy](CLI_STRATEGY.md): every `project`, `install` and `workspace` option.

## Level editing

The **Levels** page opens, edits, checks and saves maps for Doom-family games (Doom and Hexen maps in WADs, including
UDMF) and Quake-family games (Quake, Quake II and Quake III `.map` files), then hands them to the **Build** page.

> [!NOTE]
> **Status: Partial.** Opening, editing, undo, validation and saving work for every format below and are covered by
> automated tests, but none of it has been proven on real production maps. UDMF maps get property and transform editing
> only, and some tools from established editors are still missing; the
> [coverage list](LEVEL_EDITOR.md#coverage-of-other-editors) says which.

The [TB/NRC command audit](plans/level-editor-commands.md) links discovered
commands to their upstream source and records reviewed equivalents and gaps.
Unreviewed entries have no parity status yet; command counts do not establish
production readiness.

### Open a map

1. Choose **Levels** on the left rail, or press <kbd>Ctrl</kbd>+<kbd>2</kbd>. On macOS, <kbd>Ctrl</kbd> is
   <kbd>Cmd</kbd> throughout this page.
2. Choose **Open Map** in the page header, or **File** > **Open Map…** (<kbd>Ctrl</kbd>+<kbd>M</kbd>).
3. Pick a `.map` or `.wad` file. A progress window shows each loading step; **Cancel** keeps the map you had open.
4. A WAD with several maps opens its first map. To open another, choose it in the map list beside the path on the
   document bar, or type a name such as `MAP07` or `E2M1` there.

VibeStudio works out the game from the file. If it guesses wrong, change the engine list on the document bar from
**Auto** to **idTech1**, **idTech2** or **idTech3** and choose **Reload**. The empty page lists **Recent maps**, and
**File** > **Go to File…** (<kbd>Ctrl</kbd>+<kbd>P</kbd>) finds a map in your project or the open package. A map opened
from a package is a temporary copy, so its first save asks where to keep it.

To start a new map, choose **New Map** (<kbd>Ctrl</kbd>+<kbd>N</kbd> while the page has focus). Pick a **Game format**
(**Quake**, **Quake II**, **Quake III Arena**, **Doom (binary)** or **Hexen (binary)**), then **Room with player start**
or **Empty map**. Texture names refer to your own assets.

| Format | What you can do |
| --- | --- |
| Doom and Hexen maps in a WAD | Open, edit things and geometry, save |
| UDMF (`TEXTMAP`) maps in a WAD | Open, edit properties, move, rotate, mirror, snap, resize, duplicate things and save |
| Quake and Quake II `.map`, classic or Valve 220 faces | Open, edit, save |
| Quake III `.map` with `brushDef`, `brushDef3`, `patchDef2` or `patchDef3` | Open, edit, save; create and reshape patches |

### Find your way around

| Area | What it holds |
| --- | --- |
| Header | **Dependencies**, **Package Map**, **Save**, **New Map**, **Generate**, **Edit with AI**, **Open Map** |
| Document bar | The map path, the WAD's map list, the engine list, **Reload**, the compiler profile, **Run Profile**, **Copy CLI** |
| Left sidebar | **Outliner**, **Shapes**, **Entities**, **Textures**, **Models**, **Sounds**, **Prefabs** |
| Centre | The view toolbar; the tool bar (**Select**, **Draw Brush**, **Clip**, **Paint**, **Sample**, **Draw Sector**) with the grouped authoring menus; the material strip; the 2D and 3D views; and a readout of what is under the pointer |
| Right sidebar | **Inspector**, **Tools**, **Surfaces**, **Map**, **View**, **Health**, **History** |

#### Use the sidebars

Each sidebar is a column of tabs beside the views, as in Blender. Choose a tab to show its page, and choose the
current tab again to fold the sidebar down to its tabs and give the views the room. A page is made of sections: choose
a section's heading, or press <kbd>Left</kbd> and <kbd>Right</kbd> on it, to close or open it, and each remembers how
you left it. A page's heading counts what it holds, such as the outliner's objects or the health problems, and so does
its tab's tooltip.

- The **Tools** tab holds every editing command in one place, as Blender's Tool tab does, grouped into **Brush**,
  **Vertices**, **Transform**, **Align**, **Select**, **Navigate**, **Entities**, **Groups**, **Surfaces**,
  **Patches**, **Patch Rows and Columns**, **Sectors and Lines** and **Region and Build**. Each button is the command itself, so it greys out, stays pressed and takes keys just as in the menus; a
  group the map's format cannot use is hidden.
- Right-click a tab to move it to the other sidebar, fold the sidebar, show captions under the tab icons, or
  **Reset Sidebars** to where your editor profile keeps them.
- The **View** menu has **Level Sidebars** commands to show each tab and to fold either sidebar. Like every
  command, they are in command search and can be given keys in **Help** > **Keyboard Shortcuts**.
- **Show Surfaces** also hides the **Surfaces** tab again when it is showing: its sidebar goes back to the tab
  it showed before, or folds if it was folded. The NetRadiant Custom and NetRadiant profiles give it
  <kbd>S</kbd>, as NetRadiant Custom shows and hides its Surface Inspector; in mouse look <kbd>S</kbd>
  still moves the camera.
- Your [editor profile](manual/editor-profiles.md) decides where the tabs start and what they are called. Radiant profiles
  open on the texture browser and call the outliner **Entity List**. TrenchBroom keeps everything in one inspector
  on the right. Hammer-family profiles have **Objects**, **Properties**, **Face Edit** and **Primitives**, and Doom
  Builder profiles **Things**. On a Doom map, **Entities** is called **Things** whatever the profile.
- Each family of profiles remembers its own arrangement, so moving a tab under a Radiant profile leaves the
  TrenchBroom one as it was.

#### Choose a tool

The tool bar above the views says what the mouse does. **Select** picks, moves and resizes objects. **Draw Brush**
draws the shape chosen on the **Shapes** tab in the 3D view; its icon shows which. **Clip** cuts brushes along a line,
**Paint** and **Sample** put on and pick up materials in the 3D view, and **Draw Sector** traces Doom sectors. Only
the tools the open map can use are shown, and the material strip below holds the settings of the camera tools.

### Look around the map

- **2D views.** Choose **Top (X/Y)**, **Front (X/Z)** or **Side (Y/Z)** in the projection list. The wheel zooms;
  how you pan, select and draw depends on your [editor profile](manual/editor-profiles.md). **Zoom to Fit**
  (<kbd>Home</kbd>) frames the whole map and **Zoom to Selection** (<kbd>F</kbd>) frames the selection.
- **Go somewhere.** **Centre Views on Selection** moves every 2D view to the selection without changing its zoom, as
  Radiant's Center does. **Go to Coordinates…** takes the 2D views, and the camera if you tick it, to a point you
  type, as Hammer does. **Find Brush by Number…** selects and frames the object a compiler names, such as
  `Entity 0, Brush 12`: paste the compiler's line and the numbers are read from it. Brushes and patches are counted
  together from 0, in the order the file lists them, as compilers count them. All three are on the **Tools** tab's
  **Navigate** group.
- **Grid.** Choose **Grid 0.125** to **Grid 1024** in the grid list above the views, in the **View** tab's **Grid**
  list or in command search, or press <kbd>[</kbd> and <kbd>]</kbd> to halve or double it. **Smaller Grid** stops at
  0.125 units and **Larger Grid** at 1024. With **Snap** ticked, drags, arrow-key nudges, drawn brushes, clip points
  and camera drags move in whole grid steps, so on **Grid 0.25** they move a quarter of a unit at a time. When you
  zoom out, a fine grid's lines are drawn further apart so they do not fill the view; the view's status lines say how
  far apart they are. Doom and Hexen maps store whole units, so while one is open the grids below 1 unit are greyed
  out, a finer grid changes to **Grid 1** and the status bar says so; UDMF and Quake-family maps keep the fractions.
  In the NetRadiant Custom and
  NetRadiant profiles, choosing or stepping the grid also turns **Snap** on, as those editors do, and the status bar
  says so; turn this off with **Choosing or stepping the grid turns Snap on** on the **Editing** tab of
  **Customize Gestures…**. Your grid size is kept for the next session.
- **3D view.** Choose **3D** to show the camera. The VibeStudio Default profile orbits an orthographic view of the map;
  most other profiles fly a first-person camera. Tick **Textures** above the views to draw package images: open the
  game's package or asset folder on the **Packages** page first. **View** > **3D Wireframe** (<kbd>W</kbd>) shows edges
  only.
- **Choose how the camera draws.** **View** > **Camera Display** and the **View** tab's **Display** section set what the
  camera draws, and your choice is kept for the next session. **Textures in Camera** (the same as the **Textures** box
  above the views) switches the faces between package images and flat-shaded colours. **Edges in Camera** outlines
  every brush face and patch quad, not the triangles they are drawn with. **3D Wireframe** hides the faces and leaves
  the edges; turn it off and the faces come back as they were, textured or flat. The camera keeps these choices when
  it redraws after an edit. **Show Textures in Camera**, **Hide Textures in Camera** and **Hide Faces in Camera** set
  the faces outright, as TrenchBroom's view options do. **Next Camera Mode** and **Previous Camera Mode** step through
  **Wireframe**, **Flat Shaded**, **Textured** and **Textured with Edges**, and the status bar says which one is
  showing (<kbd>Shift</kbd>+<kbd>]</kbd> and <kbd>Shift</kbd>+<kbd>[</kbd> in the NetRadiant Custom profile). These
  commands work while the camera is showing; **Textures in Camera** and **Edges in Camera** can be changed at any time.
  They change only the picture, never the map. Give any of them a key in **Help** > **Keyboard Shortcuts**.
- **Aim the camera.** **View** > **Camera** moves only the 3D camera: the 2D views, the map and its undo history stay
  as they are. **Level Camera** keeps the camera where it stands, takes away its pitch and turns its heading to the
  nearest 22.5 degrees, as Radiant's **Center** does (<kbd>End</kbd> in the NetRadiant Custom, NetRadiant, GtkRadiant,
  Q3Radiant and DoomEdit profiles). **Focus Camera on Selection** keeps the camera's heading and pitch and moves it
  along its view until the whole selection fits, never closer than 64 units to its centre. Unlike **Frame
  Selection** it leaves the 2D views where they are. With nothing selected it focuses on the last selection you made
  in this map, and hidden objects never count. **Reset Camera Zoom** returns the field of view to your profile's
  after a <kbd>Shift</kbd>+wheel zoom or a saved view's own (<kbd>Ctrl+Alt+Z</kbd> in the TrenchBroom profile). In the
  VibeStudio Default profile's orbiting view, **Level Camera** levels the orbit and **Focus Camera on Selection** turns
  it about the selection. All three are in the **Tools** tab's **Navigate** group and in command search.
- **Keep the selection in view.** In the NetRadiant Custom and NetRadiant profiles, hold <kbd>Tab</kbd> during mouse
  look: the camera stays on the line through the selection's centre as you turn and fly, and the wheel moves it nearer
  or farther, eight notches halving or doubling the distance. Release the key, or leave mouse look, to fly freely
  again. Outside mouse look <kbd>Tab</kbd> still moves keyboard focus. Choose another key, or none, with **Hold during
  mouse look to keep the selection in view** on the **Camera Keys** tab of **Customize Gestures…**; any profile with
  a first-person camera can use it. `map camera-focus` reports the same camera position from the command line.
- **Layout.** The **Layout** menu shows one 2D view, one 3D view, the camera beside a 2D view, four views,
  **Camera Above Plans** (a wide camera above top, front and side), or **Camera Beside Plans**
  (a tall camera beside three stacked plans). Drag the dividers to size the panes;
  each layout remembers its own proportions. Grid, snap and selection framing remain available in a maximised camera.
  **Follow Editor Profile** returns to your profile's arrangement. **Maximize Active View** enlarges the focused pane
  until you restore it, **Equalize View Sizes** evens out the panes, and the link options keep the 2D views centred
  together or following the camera. **Saved Views** stores named camera and plan positions for this map. The
  **Layout** section of the **View** tab offers the same choices as tiles, each picturing its panes with the camera
  pane filled; point at a tile for its full name.
- **Choose the plan panes.** In the three-plan layouts (four views, **Camera Above Plans** and **Camera Beside Plans**)
  turn **Show Top View**, **Show Front View** and **Show Side View** off to hide a plan pane, for example to keep only
  the front and side views beside the camera. They sit in the **Layout** menu, the **View** menu, command search and as
  check boxes in the **Layout** section of the **View** tab, and have no keys until you give them one. One plan pane
  always stays: the last shown pane's command is unavailable. Hiding the pane you were working in makes the next shown
  pane active; a row left empty closes up. **Top View**, **Front View**, **Side View** and the projection list bring a
  hidden pane back, and **Next 2D View** steps only through the shown panes. Each combination of panes remembers its
  own proportions. The choice is saved, applies to every three-plan layout and waits unused in the one-plan layouts;
  it never changes the map or its undo history. `editor layout --panes front,side` sets it from the command line.
- **Show.** The **Show** menu chooses what the 2D views draw: **Things** (Doom) or **Entities** (Quake), **Sector Fill**
  (Doom), **Grid**, **Grid Coordinates**, **Block Grid** (Quake), **Vertices**, **Labels**, **Target Links** and **Linked
  Groups** (Quake). The **View** tab has the same options with the grid, snapping, texture locks, layouts and view links.
- **Read coordinates off the grid.** Turn on **Show** > **Grid Coordinates** to number the grid lines in every 2D view,
  as NetRadiant Custom's **Show Coordinates** does: values across along the bottom edge, values up along the left edge
  (the right edge in right-to-left languages). The numbers are spaced so they never crowd each other, show the
  fractions a fine grid needs, and stay clear of the status tags in the corners. The pointer readout below the views
  still gives the exact point under the pointer.
- **See the compiler's blocks.** Turn on **Show** > **Block Grid** on a Quake-family map to draw dashed lines where the
  compiler first splits the world into blocks, as NetRadiant Custom's **Show Blocks** does. The size comes from the
  worldspawn `_blocksize` key, which you can set in the **Map** tab: one number for every axis, three for x, y and z,
  and 0 to leave an axis unsplit. Without the key the blocks are 1024 units. The older `blocksize` and `chopsize` keys
  are read too, and a value is read exactly as q3map2 reads it, so a lone second number is ignored and the status bar
  says so. The status tag shows the size, and inside a region the lines stay within it. q3map2-family compilers such
  as VibeMap3 split on these lines; Quake and Quake II compilers such as VibeMap2 do not, so treat the lines as a guide
  for Quake III-family builds. **Export Image** and `map render --block-grid` draw the same lines.
- **Toggle them from anywhere.** **View** > **Show Grid Coordinates** and **Show Block Grid** do the same as the
  **Show** menu, with check marks, and are in command search. Neither has a key, as in NetRadiant Custom; give them
  one in **Help** > **Keyboard Shortcuts**. Both are off until you turn them on, the choice is kept for the next
  session, and neither ever changes the map or its undo history.
- **Filters.** The **Filters** section of the **View** tab hides whole kinds of object in every view, each with a
  count: world brushes, entities, brushes, brush entities, func groups, patches, detail, structural, clip, player
  clip, monster clip, weapon clip, bot clip, hint, skip, caulk, nodraw, area portals, cluster portals, light grid,
  sky, liquids, fog, translucent, decals, point entities, lights, triggers, monsters, items, player starts, paths and
  models, or on a Doom map each category of thing. A ticked kind is shown. The same kinds are checkable commands under **View** > **Filters**,
  in the **Filters** submenu of the view bar's **Select** menu and in command search, so you can give them keys;
  the NetRadiant Custom and NetRadiant profiles use theirs: <kbd>Alt+1</kbd> **World Brushes**, <kbd>Alt+2</kbd>
  **Entities**, <kbd>Alt+3</kbd> **Area Portals**, <kbd>Alt+4</kbd> **Translucent**, <kbd>Alt+5</kbd> **Liquids**,
  <kbd>Alt+6</kbd> **Caulk**, <kbd>Alt+7</kbd> **Clip**, <kbd>Alt+8</kbd> **Paths**, <kbd>Alt+9</kbd> **Cluster Portals**, <kbd>Alt+0</kbd>
  **Lights**, <kbd>Ctrl+D</kbd> **Detail**, <kbd>Ctrl+Shift+D</kbd> **Structural**, <kbd>Ctrl+H</kbd> **Hint**,
  <kbd>Shift+M</kbd> **Models**, <kbd>Ctrl+Shift+T</kbd> **Triggers**, <kbd>Alt+M</kbd> **Bot Clip** and
  <kbd>Shift+D</kbd> **Decals** (<kbd>Ctrl+P</kbd> stays **Go to File**, so **Patches** has no key, and **Light Grid**
  has none in NetRadiant Custom either; **Skip** and **Fog** have no key there). **Invert Filters** hides every kind that is
  shown and shows every kind that is hidden, as Radiant's **Invert filters** does, and **Show Every Kind** brings
  them all back.

  The texture kinds read each face's texture and content flags. **Area Portals** takes a brush with any areaportal
  face or the area portal flag, and Quake II's func_areaportal entities. **Cluster Portals**, **Light Grid** and
  **Bot Clip** (botclip or donotenter) take a brush only when every face is of that kind, and **Decals** a brush with
  any face whose texture is under `decals/`; those four are for Quake III maps, where bot clip also leaves **Clip**.
  **Structural** is the opposite of **Detail**: on Quake II and III maps every brush without the detail flag outside
  func_detail, and on Quake maps worldspawn and every brush entity but func_detail. **Caulk** hides brushes caulked on
  every face, and while it is off the camera also leaves out caulk faces on other brushes, so you see the surfaces
  players see; the 2D views keep each brush's whole outline, as Radiant's do, and the status bar says how many faces
  went. **Hint** takes a brush with a hint face, and **Skip** a brush skipped on every face; while **Skip** is off
  the camera also leaves out the skip sides of other brushes, so hiding it leaves a hint brush's hint face. When the
  faces that go one by one cover a whole brush, say caulk on some sides and skip on the rest, the brush leaves the 2D
  views too. On a Quake map, skip counts as caulk and only ericw-tools' hintskip counts as **Skip**; Quake II maps
  also read the hint and skip surface flags. **Fog** takes Quake III fog volumes: a shader with surfaceparm fog or fogParms, the fog
  content flag, or else, where no shader is known, a texture in a `fog` or `fogs` folder or named after fog, such as
  `sfx/fog_red` (not `fogged_glass`).

  The material kinds read what a surface is made of. On a Quake III map the **Filters** section reads the shader
  scripts of the package you have open, staged edits included, and says how many shaders it read; press **Reload
  Shaders** after changing scripts outside the studio (the camera's **Reload** reads them again too). A face whose
  shader the package defines, or whose image it holds without a script, then goes by that shader: **Translucent**
  takes a brush or patch with any qer_trans, alphaFunc, qer_alphafunc or fog shader, as Radiant's **Translucent**
  filter does; **Liquids** takes surfaceparm water, slime or lava, so a lavarock wall stays and a liquid under any
  name goes; **Sky** takes skyParms or surfaceparm sky; and surfaceparm playerclip, monsterclip, botclip and nodraw
  feed **Player Clip**, **Monster Clip**, **Bot Clip** and **Nodraw**. Faces the package knows nothing about keep
  the name rules, and without a package **Translucent** finds nothing on a Quake III map. Quake II maps read each
  face's own flags instead (the 33% and 66% translucency flags, nodraw and the clip contents), and Quake maps the
  texture names, so `clip` counts as player and monster clip and a quoted `{` fence texture as translucent.

  **Clip** still hides every kind of clip; **Player Clip**, **Monster Clip** and **Weapon Clip** (weapclip and
  fullclip, which stop shots too) hide one at a time, and all four take clip-shaded patches as well as brushes made
  wholly of clip. **Nodraw** hides brushes that draw nothing in game, every face nodraw, so on Quake III maps it also
  takes caulked, clip and trigger brushes whose shaders are nodraw.

  **World Brushes** counts worldspawn, func_group and func_detail as the world, as Radiant does, and **Entities**
  is everything else, doors and triggers included. **Brushes** hides every brush whoever owns it but leaves patches
  to **Patches**, as TrenchBroom's **Show Brushes** does. **Point Entities** leaves lights and models to their own
  filters, so you can hide the clutter and keep the lights; a door or other brush entity with no brushes yet stays
  under **Brush Entities**. **Func Groups** hides func_group entities only; the **Scene** tab's groups have their own
  visibility. Filters are separate from hidden objects, so **Show All Hidden**
  leaves them on, and they only change what you see and can pick, never the map. `map filters` reports the same
  kinds and counts from the command line.
- **Filter tags from your game or project.** A game or project can name its own kinds as tags, the way TrenchBroom's
  game configurations do: by texture pattern, Quake III surfaceparm, content or surface flag, or brush entity class.
  Put them in the project manifest under `levelFilterTags`, either written out or as the path of a tag file beside
  the project, or choose **Tag File…** under **Tags** in the **Filters** section. A TrenchBroom game configuration
  (`GameConfig.cfg`) works as it is: its comments, bare names and trailing commas are read as TrenchBroom reads
  them, and a pattern such as `"\**"` keeps its backslash, so it takes textures whose names start with `*`. If a file
  cannot be read, the message names the line. Each tag gets a check box with its count and a checkable **Toggle**
  *tag* **Visible** command in command search, the **Filters** menus and **Help** > **Keyboard Shortcuts**, so you
  can give it a key. A texture, surfaceparm or flag tag hides a brush whose every face matches and, in the camera,
  the matching faces of other brushes one by one; a brush whose faces all go under several tags or kinds together is
  hidden whole. A class tag hides the brush entities of that class. Surfaceparm tags read the shaders of the package
  you have open, as the material kinds do. Tags are remembered with the other kinds, **Invert Filters** flips them
  too and says how many it hid, and **Forget Tag File** stops using the file. The **Tags** line says where the tags
  came from and how many entries it skipped; point at it for the details. Names written entirely in non-Latin
  scripts keep their display text and receive a stable generated command ID.

  ```json
  "levelFilterTags": {
    "brush": [ { "name": "Triggers", "match": "classname", "pattern": "trigger_*" } ],
    "brushface": [ { "name": "Fog", "match": "surfaceparm", "pattern": "fog" },
                   { "name": "Water", "match": "texture", "pattern": "*water*" },
                   { "name": "Trans", "match": "surfaceflag", "flags": [ "trans33", "trans66" ] } ]
  }
  ```

  Texture patterns match the texture's own name, or its path when the pattern has a `/`; `*` and `?` are wildcards,
  `\` makes the next character plain, and case does not matter. Flags are the game's names (as the **Surfaces** tab
  shows them), numbers, or the names a game configuration's `faceattribs` lists.
- **Entity classes.** The **Entity Classes** section of the **View** tab lists every class of the loaded entity
  definitions and every class the map uses, grouped by the part of the name before the first `_` and counted.
  Untick a class, or a whole group, to hide every entity of it in all the views, including ones you add later;
  **Hide All Classes** and **Show All Classes** do the lot, and **Find a class** narrows the list. Each class also has
  a checkable **Toggle** *class* **Visible** command, as in TrenchBroom, that you can find in command search and give
  a key. Class filters last until you open another map and never change the map; **Show Every Kind** clears them
  too. A class whose name holds a comma has no filter, as commas separate the lists filters are kept in.
  `map visibility --class` and `map filters --class` report what they hide.
- **Region.** To work on one area of a big map, choose **Set to Selection** or **Set to View** in the **Region**
  section of the **View** tab (or **Select** > **Set Region to Selection**). **Draw Region** lets you drag the box in
  a 2D view, through the whole map's depth; **Set Region to Brush** uses the selected brushes' bounds and removes
  them, as Radiant does (**Undo** brings them back and keeps the region); and **Region Bounds…** takes the corners
  you type. The views shade or hide what lies outside the box, as Radiant's regions and Hammer's cordon do, and
  **Select Everything in Region** selects what it keeps. VibeStudio remembers each map's region and brings it back
  when you open the map again; **Clear Region** brings the whole map back and forgets it.
  **Compile Region** writes the region beside the map as `<name>-region.map`, unsaved edits included, and compiles
  it with the chosen profile, so `<name>-region` can be loaded in the game to test that area quickly. The region
  map keeps what touches the box, sealed by six brushes just outside it, and gets a player start (where the camera
  stands, or in the middle) if it has none. **Save Region As…** writes the same map anywhere you choose. **Run
  Profile** still builds the whole map.
- **Hide and show.** **Hide Selection** temporarily hides selected objects; **Show All Hidden** clears temporary
  hiding, and **Isolate Selection** hides everything outside the selected objects. All plan panes and the camera
  share this state. Hidden objects stay in the map, its builds and its packages. See [Temporary visibility](manual/levels.md#temporary-visibility)
  for profile shortcuts, undo and the separate scene/filter controls.
- **Export Image** writes an SVG picture of the map for reviews and documentation.

### Temporary visibility

**Hide Selection**, **Show All Hidden** and **Isolate Selection** use session
visibility. NRC and default controls use <kbd>H</kbd> and <kbd>Shift</kbd>+<kbd>H</kbd>
for Hide and Show All. TB uses <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>I</kbd>,
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd> and <kbd>Ctrl</kbd>+<kbd>I</kbd>
for Hide, Show All and Isolate respectively. The context menu follows your
current profile and shortcut overrides.

Hiding clears the object selection. Closed groups hide as a unit; isolating a
brush entity keeps its children, while isolating an individual child keeps its
owner visible and hides its siblings. Isolate considers the whole map, including
locked objects and objects outside the open group. Selecting a hidden object
in the **Objects** list reveals that object without revealing hidden siblings
or ancestors; selecting an entity preserves independently hidden children.
Ordinary plan and camera picks preserve hidden members of a closed group.
Hiding a Doom sector or
linedef also suppresses geometry that depends entirely on hidden records;
isolating a line retains its required vertices and sector references. Isolating
a Doom vertex also retains its incident lines, their endpoints and sector references.

The TB profile records each operation, including selection and object-list
reveal, as one undo step without marking the map modified. NRC and default
profiles keep visibility outside undo. Structural undo/redo tracks surviving
hidden object identities, including compacted Doom record IDs. Operations run
in a cancellable worker; cancellation or a changed document leaves the live
visibility untouched. Finish component, clipping, drawing and surface tools
before using these object commands.

**Show All Hidden** clears temporary hiding only. It leaves type/material/region
filters and the **Scene** tab's saved visibility check boxes in place. Toggle
those controls separately when an object remains invisible. Saving, compiling
and packaging include temporarily hidden objects; reopening starts a new
visibility session. The CLI's `map visibility` command returns the same session
result as selectors without writing a map.

The TB profile hides closed groups as containers. Existing objects moved into
one inherit its temporary hiding; newly authored objects are explicitly shown,
so drawing in a hidden destination does not make the result disappear. Revealing
a child in an open group through the **Objects** list leaves its container hidden.
A closed group remains one selection unit, so row activation reveals its members. Hiding the container
again clears these shown overrides, preserving independently hidden children.
Nested groups and empty descendant groups participate in isolation. **Show All
Hidden** resets both hidden and shown overrides. Undo restores exact states and
selection; deleting and restoring an object or group preserves its identity.
NRC and default profiles hide the current group members as individual objects.
Existing session states retain their meaning when switching profiles.

Moving a group or assigning objects to another layer resets their shown
exceptions, including descendants that inherit their owner's membership.
Moves within a layer preserve them. Undo restores visibility and selection in
the same step; later transient hiding is retained. In TB and default profiles,
**Duplicate**, **Array** and **Create Linked Duplicate** preserve explicit hidden/shown attributes. The inserted
root is revealed while independently hidden descendants stay hidden. Linked
updates rebuild fresh inherited children, so source visibility exceptions do
not spread to other linked groups. New empty groups in hidden destinations also
receive a shown exception. Portable map/clipboard data does not store these
session attributes.

The NRC profile gives new objects and recursive copies fresh inherited temporary
visibility. A hidden source descendant becomes visible in its copy unless the
destination parent still hides it. New geometry, paste and empty groups also
inherit a hidden destination. The profile's **New and duplicated objects** row
in the controls reference describes the policy. Changing profiles affects future
authoring; it preserves existing states and recorded undo/redo results. A pending
placement prepared under a previous policy must be reopened.

Status: Partial. Saved scene visibility still takes precedence over temporary
shown overrides. Direct empty-group hide/show controls, component-mode hiding,
native gestures, full saved-layer/filter equivalence and real-project acceptance remain open.
NRC's automatic hiding of a brush entity after its final child is hidden also
needs parity work: VibeStudio currently derives the hidden label from its children.

### Select and inspect objects

The compact **Create**, **Select**, **Transform**, **Geometry** and **Surfaces** menus above the views bring the
authoring tools together. They use the same commands, profile shortcuts and selection checks as the main menus.
For example, **Transform** > **Duplicate with Offset…** prepares repeated objects, while **Geometry** offers
clipping, hollowing, component editing and patch tools. Unsupported actions are disabled for the current map or selection.

- Click objects in a view or in the **Objects** list on the **Outliner** tab; <kbd>Ctrl</kbd>+click and <kbd>Shift</kbd>+click build a
  multiple selection in either place. In a 2D view, <kbd>Tab</kbd> selects the next object, and
  <kbd>Ctrl</kbd>+<kbd>Tab</kbd> and <kbd>Shift</kbd>+<kbd>Tab</kbd> add the next or previous one. In a Doom map, a
  click inside a room selects its sector.
- Type in the **Objects** filter to narrow the list by name, or test properties with `key=value`, `key:text`,
  `key!=value`, `key<n` and `key>n`, such as `class=light`, `tag=3`, `texture:brick` or `light>200`. Every term must
  match. <kbd>Enter</kbd> selects everything the filter keeps, and <kbd>Down</kbd> lists recent queries.
- **Select All** (<kbd>Ctrl</kbd>+<kbd>A</kbd>) selects every entity, brush and patch that is not hidden in a
  Quake-family map (never `worldspawn` itself), and every thing in a Doom map; Doom vertices, lines and sectors are
  picked by hand. **Invert Selection** (<kbd>Ctrl</kbd>+<kbd>I</kbd>) works within the same set, and **Select None**
  (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>A</kbd>) clears the selection.
- The **Edit** menu adds **Select All of This Class**, **Select by Texture…**, **Select Targets**, **Select Sources**,
  **Expand Selection to Whole Entities** (the whole brush entity of every selected brush or patch, as Radiant's
  Expand Selection to Entities does) and, for Doom maps, **Select Connected Geometry**.
- **Select Siblings** and **Select by Source Line Number…** find Quake-family objects by their parents or their
  position in the loaded map text. Both are in **Select**, **Tools > Select** and command search. See
  [Select by hierarchy or source line](manual/levels.md#select-by-hierarchy-or-source-line).
- **Select Connected Entities** adds direct incoming and outgoing entity connections to the current selection.
  NRC binds **Ctrl+Shift+E**. See [Select connected entities](manual/levels.md#select-connected-entities).
- **Select All of Type** selects every selected owner's class, or uses the current material when no entity class
  is selected. In face mode it selects matching faces. NRC binds **Shift+A**. See [Select all of a type](manual/levels.md#select-all-of-a-type).
- **Select by Region**, in the right-click menu and the **Select** menu above the views, follows the active editor
  profile. TB uses each brush's convex volume and removes the selection brushes; NRC uses each brush's bounds,
  removing selected objects for **Inside** and keeping them for **Touching**. The default profile uses combined
  bounds and retains geometry. See [Select by brush volume](manual/levels.md#select-by-brush-volume) before using a consuming command.
- The **Transform** section at the top of the **Inspector** tab shows where the selection's centre is and how big it
  is. Type a new centre to move it there, or a new size to resize it, keeping its lower corner; point entities
  move but have no size to change. Narrow sidebars and larger text stack the axis fields so the complete numeric
  range stays readable. Widen the sidebar to restore the compact matrix. Reflow preserves pending edits and focus;
  the tab order is Centre X/Y/Z, then Size X/Y/Z in either layout. If a pane is narrower than a native control's
  minimum size, a horizontal scrollbar keeps its contents reachable.
- The **Inspector** tab lists the selection's keys and values. Double-click a value, or press <kbd>Enter</kbd>, to
  edit it; colour keys open a colour picker and spawnflags show as check boxes. Right-click a row for **Find Objects
  With This Value** or **Copy Value**. A selected brush lists its faces with texture, shift, rotation and scale; click a
  face in the 3D view to jump to it.
- Right-click an object in a 2D view or the **Objects** list for the map actions menu. **Copy Selector** copies its
  name, such as `entity:3`, for the command line.

### Edit Quake-family maps

Most actions are in the **Edit** menu, the right-click menu of the views and the **Objects** list, and command search
(<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd>). Each edit is one undo step.

| To | Do this |
| --- | --- |
| Add an entity | Choose **Add Entity…**, or drag a class from the **Entities** tab onto a 2D view (<kbd>Enter</kbd> or **Place in View** places it in the middle of the view). The new entity is selected, so its keys are on the **Inspector** at once. The classes come from your [entity definitions](manual/levels.md#check-maps-and-entities), or from the built-in classes of Quake, Quake II or Quake III Arena, each marked with its editor colour. |
| Add a brush | Choose a shape on the [**Shapes** tab](manual/levels.md#build-with-shapes) and drag over empty space in a 2D view (profiles such as TrenchBroom and the Radiant family), or in the 3D view with **Draw Brush** on, dragging a footprint and using the wheel for depth. **Add Brush…** previews a box, wedge, cylinder, cone or sphere first. |
| Make a brush entity | Select brushes and choose a class under **Brush Entities** on the **Entities** tab, or **Brush Entity** > **Tie to Entity…** in the right-click menu, as in Hammer and Radiant. With no brushes selected, the class arrives as a new brush where you drop it. **Move to World** gives an entity's brushes back to the world; an entity left with none goes. |
| Create an entity by class | Every point and brush class of your entity definitions has its own **Create** command, such as **Create light** or **Create func_door**, as in TrenchBroom: type the class in command search, or give it keys in **Help** > **Keyboard Shortcuts** (no profile gives them keys, as TrenchBroom does not). A point class goes in at the pointer in the view you used last, or in the middle of that view, its definition box standing on the brush surface or plane there and lined up with the grid, as **Paste at Cursor** places objects. A brush class makes the selected brushes and patches, with those of selected brush entities, into one, so it waits until brushes are selected; the **Brush Entity** menu uses the same commands and shows their keys. **Create Entity…** (**Edit** > **Brush Entities**, the **Create** menu above the views, or the **Tools** tab) lets you type a class instead. The commands follow the loaded definitions, and keys you give a class are kept while a map without it is open. Each is one undo step; a locked or hidden brush, one outside the open group, or a locked current layer refuses it with the reason. |
| Move brushes between entities | Select the brushes and patches, then an item of the entity that should take them, and choose **Move Primitives to Entity** (<kbd>Ctrl</kbd>+<kbd>M</kbd> in the NetRadiant Custom profile): the entity of the item you picked last takes the others and keeps its keys, as NetRadiant Custom does. **Move Primitives to First Entity** uses the item you picked first. **Move Selected to World** gives only the selected brushes and patches back to the world, while their entities keep the rest and their keys. An entity left empty goes, hidden brushes of a selected entity stay where they are, and locked or linked objects refuse the move. Brushes keep their numbers, groups and visibility; one leaving an entity for the world joins its entity's group, or else the open group or current layer. |
| Explode an entity | **Explode Entity** gives every brush and patch of the selected brush entities an entity of its own with the same keys, as bobToolz does. The entity keeps its first brush, and the new pieces are selected with it. **Explode Entity, Unique Names** gives each new piece its own `targetname`, so triggers still fire only the original. Worldspawn, locked and linked entities are refused. |
| Place models and sounds | The **Models** and **Sounds** tabs list the open package's models and sounds, with a preview of the chosen model and a waveform you can play. Drag one onto a view, or choose **Place in View**, for a `misc_model` or a speaker entity; **Give to Selection** sets the selected entities' `model` or `noise` key instead. The **Place** section names the class it makes in the open map, and says so when your definitions do not declare it: Quake itself has no model or speaker entity, so a Quake mod must provide them. |
| Duplicate or delete | **Duplicate Selection** copies objects using your profile's placement; **Duplicate with Offset…** previews an exact offset and a **Copies** count for a linear array. <kbd>Delete</kbd> removes the selection; an entity takes its brushes with it. |
| Move | Drag the selection, nudge it one grid step with the keys your profile gives the **Nudge** commands (see [Nudge the selection](manual/levels.md#nudge-the-selection)), or use **Move Selection…**. |
| Rotate or flip | **Rotate 90° Left**, **Rotate 90° Right**, **Flip Horizontal** and **Flip Vertical** work as the active view shows the map, the camera included, and **Roll** and **Pitch** turn about the view's other axes; see [Turn and flip from the view](manual/levels.md#turn-and-flip-from-the-view). **Flip X**, **Flip Y**, **Flip Z**, **Rotate X**, **Rotate Y** and **Rotate Z** (**Edit** > **Transform**, the **Transform** menu above the views, or the **Tools** tab) use the world axis whatever view is active, as NetRadiant Custom's commands do: Rotate X and Rotate Z turn -90° and Rotate Y +90°, each clockwise as the side, top or front view shows it. Doom maps flip along X or Y and turn about Z only. No profile gives them keys, as NetRadiant Custom does not; add your own in **Help** > **Keyboard Shortcuts**. In the NetRadiant Custom and TrenchBroom profiles, every quick turn pivots on the grid point nearest the selection's centre while **Snap** is on, so a brush whose sides differ by an odd number of grid steps stays on the grid; it may move by up to half a step. Other profiles turn about the exact centre; **Customize Editor Gestures** changes this for any profile, and a custom component pivot always wins. Flips mirror about the exact centre, except in the TrenchBroom profile, which rounds it to half the grid while **Snap** is on. **Rotate Selection…** turns by any X, Y and Z angles about the selection centre, the world origin or your own pivot; see [Rotate and scale by exact values](manual/levels.md#rotate-and-scale-by-exact-values). |
| Align | **Transform** > **Align Left**, **Align Right**, **Align Top**, **Align Bottom**, **Centre Horizontally** and **Centre Vertically** line the selected objects up on the edge or centre of the whole selection, as the active 2D view shows it, as Hammer does. An entity moves with its brushes. |
| Shear | Turn on **Transform** > **Shear Tool** and drag the handle in the middle of a side of the selection in any 2D view: that side slides along itself and the opposite side stays put, slanting the selection, as TrenchBroom's shear tool does. <kbd>Escape</kbd> turns the tool off. **Transform** > **Shear…** slants the selection about its centre by an angle you type. Texture lock follows **Texture Lock**; where the map's face format cannot slant textures, the tool shears without it and says so. |
| Resize | Drag the handles in a 2D view, or the labelled X, Y and Z handles in the 3D view. **Resize Selection…** takes exact sizes and the point that stays in place. **Scale Selection…** multiplies the size by factors, and a negative factor mirrors; see [Rotate and scale by exact values](manual/levels.md#rotate-and-scale-by-exact-values). |
| Cut with a plane | Turn on the **Clip Tool** (<kbd>X</kbd>), place or drag two or three points in any view, use <kbd>Tab</kbd> to choose sides, then <kbd>Enter</kbd> to apply the checked result. **Clip Selection…** accepts an axis plane or three exact XYZ points, with **Preview** before applying. |
| Hollow | **Hollow…** turns each selected brush into walls of the thickness you choose, one per face, overlapping at the edges. **Hollow Touching…** makes walls that meet without overlapping, and **Make Room** builds walls one grid step thick around the outside of each brush and removes it, so the brush becomes the room, as NetRadiant Custom does. See [Make a room with caulk and openings](manual/levels.md#make-a-room-with-caulk-and-openings). |
| Carve (CSG subtract) | **Carve** cuts the selected brushes out of every brush they overlap. The carving brushes stay selected, ready to delete; hidden brushes are left alone. |
| Intersect (CSG) | **Intersect** replaces the selected brushes with the one brush where they all overlap, as TrenchBroom does; each face keeps the texture of the face it came from. |
| Make detail | **Make Detail** stops brushes sealing the map or splitting its visibility: in Quake II and Quake III maps it sets each face's detail flag, and in Quake maps it moves the brushes into a `func_detail` for VibeMap2, as ericw-tools expects. **Make Structural** undoes it, taking only the selected brushes out of a `func_detail`. In the TrenchBroom profile (<kbd>Alt</kbd>+<kbd>S</kbd>) it also moves the selected brushes out of any brush entity to the world, into the open group or current layer, as TrenchBroom does; other flag-based tags stay as they are. |
| Drop to the floor | **Drop to Floor** moves the selected point entities straight down onto the brush or patch below, keeping each class's height above it. |
| Round to the grid | **Round Selection to Grid** rounds every brush vertex, patch control point and point-entity origin of the selection onto the grid on its own, as Radiant's Snap To Grid does, so off-grid shapes are cleaned up where they stand; brush entities round their brushes and keep their origin key. In **Edit Vertices**, **Edit Edges** or **Edit Faces** it rounds only the picked components. It is one undo step, follows **Lock Component UVs**, and leaves a brush the grid would collapse as it was, saying why. The NetRadiant Custom profile runs it with <kbd>Ctrl</kbd>+<kbd>G</kbd>; **Snap Selection to Grid** instead moves each object whole so its corner or origin lands on the grid. In Doom-format maps it snaps objects as **Snap Selection to Grid** does. |
| Merge brushes | **Merge Brushes…** previews their convex union and lets you choose where conflicting materials, mappings and flags come from. |
| Apply a texture | Select brushes or patches and choose **Apply Texture…**, or press <kbd>Enter</kbd> on a tile on the **Textures** tab. **Find and Replace Textures…** finds or swaps one texture across the map, the selection or the picked faces, and **Reveal Material** shows the selection's texture on the **Textures** tab; see [Find, replace and reveal textures](manual/levels.md#find-replace-and-reveal-textures). |
| Align textures | Edit a classic or Valve 220 face's shift, rotation and scale on the **Inspector** tab, or use the **Surfaces** tab. In Face mode, both targets use exactly the [picked face components](manual/levels.md#edit-surfaces-from-component-faces). In object mode, **Target** chooses the selection, patches included, or **Picked Faces**: the faces you pick in the camera with your profile's face-pick click, holding its add key to gather more, or else the face in the **Inspector**. **Adjust** shifts, rotates, scales, fits or centres in steps, adds or subtracts a Radiant **Scale step** (see [Step textures by Radiant increments](manual/levels.md#step-textures-by-radiant-increments)), and has one-click **Flip U** and **Flip V**, **Fit U** and **Fit V** (one repeat across that axis only), **Fit Width** and **Fit Height** (one repeat, keeping the texture's proportions), **Justify Left**, **Right**, **Top** and **Bottom**, **Natural** (the texture at its natural scale, square to the world, as Radiant's Natural does), **Align to Face** (the texture laid along the face's own slope), **Axial**, **2D View** and **Camera** under **Project texture** (see [Fit and project textures](manual/levels.md#fit-and-project-textures)), and **Reset** and **Radiant Reset** (see [Reset textures](manual/levels.md#reset-textures)). **Copy and Paste** carries one face's material, mapping and flags to others. **Surface Alignment…** gives a detailed preview, and **UV Editor…** drags one face's or patch's points over its texture (see [Edit texture coordinates in the UV Editor](manual/levels.md#edit-texture-coordinates-in-the-uv-editor)). To move or flip textures the way the camera shows them, see [Move and flip textures from the camera](manual/levels.md#move-and-flip-textures-from-the-camera). |
| Paint materials | Choose a material in the **Material** box above the views, set the camera tool to **Paint**, then click or drag across surfaces in the 3D view. Each stroke is one undo step. **Sample** picks up a surface's material instead. |
| Reshape a brush | Turn on **Edit Vertices**, **Edit Edges** or **Edit Faces** (**Geometry** or **Tools > Vertices**) to show corners, edge middles or face centres in the plan views and camera. Picks are shared between panes. In a plan view, click stacked handles, use the profile's add/toggle modifier or drag a selection band, then drag them or nudge them with your profile's **Nudge** keys (<kbd>Shift</kbd>: eight grid steps; see [Nudge the selection](manual/levels.md#nudge-the-selection)). The camera supports visible/X-ray picks and XYZ movement; see [camera components](manual/levels.md#move-components-in-the-camera). A corner moved onto another merges with it; **Undo** restores it. **Snap Vertices to Grid** rounds brush corners onto the grid. **Edit Brush Components…** provides exact numbers and a preview. |
| Make curves (Quake III) | **Add Patch…** creates a plane, cylinder, square cylinder, cone, sphere, bevel or end cap, its cylinders, cones and spheres true circles of as many spans as you choose; the **Shapes** tab draws them too. **Edit Patch Control Points…**, **Stitch Patches…** and **Cap Patch…** reshape, join and close patches, and **Edit Vertices** drags their control points in the plan views and camera. The **Patch Rows and Columns** commands, also in the patch editor, insert, add or delete rows and columns without changing the surface, **Transpose**, **Re-disperse** and **Smooth** them, as Radiant's Patch menu does, and **Thicken Patch…** gives patches a back and walls along their open edges, as NetRadiant Custom does. |
| Link entities | Pick the source objects first and the destination last, then choose **Connect Entities**. NRC requires exactly two objects; see the connection rules below. |
| Replace key values | **Edit** > **Replace Key Values…**, or the search button on the **Inspector**'s **Properties** section, finds a value of one key across the map's entities (or the selected ones) and replaces it: the whole value, or with **Match the whole value** off, every occurrence, such as a name's prefix. It counts the matches as you type, and the change is one undo step. |
| Colour lights and entities | Select lights or other entities, or brushes of a brush entity, and choose **Set Entity Colour…** (**Edit** > **Keys**, the right-click menu, or <kbd>K</kbd> in the NetRadiant Custom and NetRadiant profiles). The picker opens on the colour of the entity you picked last, or on the colour you chose last, and the choice is written to the `_color` key of every one of those entities as one undo step. A key already written as 0-255 stays 0-255; a new one is written as 0-1 fractions. **Normalise Entity Colour** scales the last-picked entity's colour so its brightest channel is 1 (black becomes white) and sets it on all of them, as NetRadiant Custom does. World brushes and hidden entities are left alone, and a locked entity refuses the change. |
| Reuse a group of objects | **Export Selection as Prefab…** or **Save Selection…** on the **Prefabs** tab saves a `.vprefab` file, and the tab lists it at once with those in the project and the open package. **Insert Prefab…** places one with a preview, or drag one from the tab onto a 2D view to place it where you drop it. **Replace Selection with Prefab…** puts one where the selection stands, its anchor at the selection's bottom centre, and removes the selection, as one undo step. |

TB and NRC duplicate at the original position by default. VibeStudio Default
moves copies one grid step right and down in the active plan. Change this per
profile in **Customize Gestures…** > **Editing** > **Nudge duplicated objects one
grid step right and down**. A command invoked from the camera uses the last active
plan's axes. **Profile default** restores the profile's own placement.

Held NRC surface-transfer gestures retain the surfaces under the original
camera view while their material preview changes. Picking at face edges follows
the same rule for the initial and subsequent hits, including surfaces cut by
the camera's near plane. Release commits the stroke as one undo step; **Cancel
Stroke** discards the full preview.

The NRC profile enables duplicate commands only in object mode; leave the
vertex, edge or face tool first. A duplicate remains one undo step. **Repeat**
uses its recorded offset even if you subsequently change the preference, and a
pending placement rejects changes to that preference before publication.

**Connect Entities** uses the order you pick objects. In the NRC profile
(**Ctrl+K**), select exactly two objects with different entity owners. Brushes
and patches use their owner, including worldspawn. The source's existing `target`
value takes priority and becomes the destination's `targetname`, even if that
replaces an existing name. Otherwise the destination's name is reused; if both
are empty, a name based on its classname is allocated against the map's names
and target references. Names preserve case and whitespace. Link display and source/target navigation
use exact native property keys, so differently cased custom keys cannot mask a
real connection. Other profiles keep
multi-source connections: the last distinct entity is the destination, its name
takes priority, and worldspawn is excluded.

**KillConnect Entities** (NRC: **Shift+K**) uses the same two-object rules with
`killtarget` instead of `target`. It leaves the source's ordinary target intact.
The action requires a Quake, Nexuiz or Xonotic game context. The current project's
game wins, then its selected installation, then the map's built-in game evidence;
Quake II, Quake III and Doom contexts disable it. Both commands are in **Edit**,
the level **Tools** tab, the context menu and command search. Inspect existing
connections before changing a destination name: other references are retained.

Each changed connection is one undo step. A redundant NRC connection succeeds
without adding history. Selection remains unchanged. Locked records and open
group boundaries are respected; linked groups share ordinary properties
while protected values stay local to their group. Preparation runs with cancellable
progress and rejects a pending result if the map, selection, profile or game
changes. Native-input and cross-platform acceptance remain **Partial**.

**Duplicate, Make Unique** (NRC: **Shift+Space**) copies selected objects into new
entity owners. Selecting part of a brush entity copies its properties and only
the selected brushes or patches; world brushes keep worldspawn as their owner.
Copied `targetname`, `target`, `killtarget` and numbered `target` properties are
renamed together using unused numeric suffixes. For example, a copied `gate`
target and its trigger both become `gate1` when that name is free. Outgoing
references are renamed too, even when their destination is outside the selection;
review those connections after copying. Other properties keep their values.
This command uses the same nudge preference, requires object mode and a Quake-family
text map, and has one undo step. **Repeat** allocates fresh names each time.

For repeated steps, columns or props, set **Copies** in **Duplicate with Offset…** to 1–256 additional copies.
With an X offset of 64, the first copy moves 64 units from the source, the second 128, and the third 192.
The preview shows every copy; **Place** selects them all in one undo step. Copies preserve their owners, textures and
scene memberships. The same workflow copies Doom/Hexen/UDMF things. UDMF copies retain fractional XYZ positions,
comments and unknown thing properties; geometry duplication is unavailable. Entity target
names retain ordinary duplicate behaviour. Use **Duplicate, Make Unique** for
NRC-style names, or **Insert Prefab…** for reusable assemblies. Automation can
combine `map duplicate --make-unique --copies N` with an explicit offset.
An invalid copy or cancellation leaves the entire map unchanged.

**Texture Lock** (on by default) keeps textures fixed to faces as you move, rotate and flip; **Texture Scale Lock**
stretches them when you resize. When exact locking needs explicit texture axes, the edit is refused unless **Allow Valve
220 Conversion** is ticked, which converts every classic face in the map in the same undo step. Your compiler must
support Valve 220.

<details>
<summary>Edits the editor refuses to protect your file</summary>

VibeStudio rewrites only the lines you change, so the rest of the file stays byte for byte. It refuses an edit it
cannot write back safely:

- deleting or copying an object whose brace shares a line with other text;
- changing a texture on a line that holds more than one face, or a texture name the map's syntax would split;
- moving, rotating, flipping, resizing or copying a brush that writes two faces on one line (clip, hollow and carve
  refuse it too);
- a number edit on a face whose numbers continue onto the next line.

A key whose value is written on the next line keeps its old value when you save.

</details>

#### Nudge the selection

Use the **Nudge** commands to move the selection one grid step at a time. They are in **Edit** > **Nudge**, the
**Tools** tab and command search:

| Command | In a plan view | In the camera |
| --- | --- | --- |
| **Nudge Left**, **Nudge Right** | Left or right across the view | Along the camera's right axis, snapped to the nearest world axis |
| **Nudge Forward**, **Nudge Back** | Up or down the view | Along the horizontal world axis nearest the camera's view |
| **Nudge Depth Up**, **Nudge Depth Down** | Towards or away from you: Z in the top view, Y in the front view, X in the side view | Up or down world Z |
| **Nudge Up Along Z**, **Nudge Down Along Z** | Up or down world Z | Up or down world Z |

The keys come from your editor profile:

- **TrenchBroom**: the arrow keys, and <kbd>Page Up</kbd> and <kbd>Page Down</kbd> for depth, as TrenchBroom has them.
  In the single 3D layout the arrows nudge the camera's way, so you do not need to switch to a 2D view first.
- **NetRadiant Custom**, **GtkRadiant 1.6** and the other Radiant-family profiles, DoomEdit included, whose arrows
  drive the camera: <kbd>Alt</kbd>+arrow keys, and the keypad's <kbd>+</kbd> and <kbd>-</kbd> along world Z. From
  the camera they use the plan view you used last, as NetRadiant Custom does, and they come before **Go Back**,
  **Go Forward** and view zoom while a view has focus.
- **VibeStudio Default** and the other profiles: the arrow keys in the plan views. The default orbiting camera keeps
  the arrows for turning and panning, and so does any camera that flies or drives with them.

<kbd>Shift</kbd> with a nudge key moves eight grid steps, unless your profile gives that key another command, such
as the Radiant profiles' texture shifts. With nothing to nudge, the arrows pan the plan view as before. Give the
commands your own keys in **Help** > **Keyboard Shortcuts**.

A nudge moves the first of these that is active:

1. the sweep destination, while the **Sweep Tool** is on;
2. the shared component pivot, while **Rotate Components** or **Move Component Pivot** is chosen (this does not
   change the map, and **Reset Component Pivot** puts it back);
3. the picked vertices, edges, faces or patch points, and faces picked by **Select Textured**, in **Edit Vertices**,
   **Edit Edges** or **Edit Faces**;
4. the selected objects.

Each nudge of the map is one undo step. It follows **Texture Lock** for objects and **Lock Component UVs** for
components, and a locked object, or one outside the open group, refuses it with the reason. Split and
extrusion nudges stay in a plan view's plane. Doom maps nudge along X and Y only: there <kbd>Page Up</kbd> and
<kbd>Page Down</kbd> still raise and lower the selected sectors' floors, in the TrenchBroom profile too.

To nudge from a script, use `map nudge`, which resolves the same directions for a plan view or a camera; see
[the command line](manual/cli.md).

#### Copy and move in one step

Use the **Duplicate and Move** commands to copy the selection and move the copies one grid step, as one undo step.
They are in **Edit** > **Duplicate and Move**, the **Tools** tab and command search. **Duplicate and Move Left**,
**Right**, **Forward**, **Back**, **Depth Up** and **Depth Down** move the copies the way the matching nudge moves the
selection: across or up the plan view, along its depth axis, or along the camera's nearest axes. The copies are
selected afterwards, so pressing the key again copies the copies; **Repeat Last Commands** replays the copy and its
offset too.

- **TrenchBroom**: <kbd>Ctrl</kbd>+arrow keys, and <kbd>Ctrl</kbd>+<kbd>Page Up</kbd> and
  <kbd>Ctrl</kbd>+<kbd>Page Down</kbd> for depth, as TrenchBroom has them. The views take these keys only while a copy
  can be made: with nothing selected, <kbd>Ctrl</kbd>+arrow keys still pan a plan view, and on a Doom map
  <kbd>Ctrl</kbd>+<kbd>Page Up</kbd> and <kbd>Ctrl</kbd>+<kbd>Page Down</kbd> still raise and lower ceilings.
- The other profiles give them no keys. Add your own in **Help** > **Keyboard Shortcuts**.

The commands copy whole objects, so they are off in **Edit Vertices**, **Edit Edges** and **Edit Faces** and while the
**Sweep Tool** is on. They follow **Texture Lock** and the open group, and a locked object refuses them with the reason.
Doom maps copy things only, along X and Y. To copy and move from a script, add `--duplicate` to `map nudge`; see
[the command line](manual/cli.md).

#### Turn and flip from the view

Use the view-relative turns and flips to turn the selection a quarter turn, or mirror it, about the axes of the view you
work in. They are in **Edit** > **Transform**, the **Transform** menu above the views, the **Tools** tab and command
search:

| Command | In a plan view | In the camera |
| --- | --- | --- |
| **Roll Clockwise**, **Roll Counter-clockwise** | About the view's vertical axis: Y in the top view, Z in the front and side views | About the horizontal world axis nearest the camera's view |
| **Rotate 90° Right**, **Rotate 90° Left** (yaw) | About the axis that points at you, clockwise or counter-clockwise as the view shows it | About world Z, as seen from above |
| **Pitch Clockwise**, **Pitch Counter-clockwise** | About the view's horizontal axis | About the camera's right axis, snapped to the nearest world axis |
| **Flip Horizontal** | Left to right across the view | Along the camera's right axis, snapped to the nearest world axis |
| **Flip Vertical** | Top to bottom | Along world Z |

Clockwise means as you see it looking along the axis: forward for **Roll**, to the right for **Pitch**. The commands
use the view that has focus or that you used last; from the menus and command search, that is the last view you
clicked in. In the single 3D layout they always follow the camera. The NetRadiant Custom and other Radiant-family
profiles keep the last plan view's axes from the camera, as their nudges do.

The keys come from your editor profile:

- **TrenchBroom**: <kbd>Alt</kbd>+<kbd>Up</kbd> and <kbd>Alt</kbd>+<kbd>Down</kbd> roll,
  <kbd>Alt</kbd>+<kbd>Left</kbd> and <kbd>Alt</kbd>+<kbd>Right</kbd> yaw, <kbd>Alt</kbd>+<kbd>Page Up</kbd> and
  <kbd>Alt</kbd>+<kbd>Page Down</kbd> pitch, <kbd>Ctrl</kbd>+<kbd>F</kbd> flips horizontally and
  <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F</kbd> vertically, as TrenchBroom has them. The views take these keys only while
  the command can act; elsewhere, or with nothing selected, <kbd>Alt</kbd>+<kbd>Left</kbd> and
  <kbd>Alt</kbd>+<kbd>Right</kbd> still go back and forward and <kbd>Ctrl</kbd>+<kbd>F</kbd> still finds.
- **Hammer** and **J.A.C.K.**: <kbd>Ctrl</kbd>+<kbd>L</kbd> flips horizontally and <kbd>Ctrl</kbd>+<kbd>I</kbd>
  vertically. **J.A.C.K.** also turns with <kbd>Ctrl</kbd>+<kbd>R</kbd> (**Rotate 90° Left**) and
  <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>R</kbd> (**Rotate 90° Right**).
- The other profiles give them no keys, and only TrenchBroom binds **Roll** and **Pitch**. Add your own in **Help** >
  **Keyboard Shortcuts**.

A turn rotates the first of these that is active:

1. the sweep destination, while the **Sweep Tool** is on, by the rotation step of the component gizmo (15° unless you
   change it), as TrenchBroom turns its sweep cap by the grid angle;
2. the picked vertices, edges, faces or patch points, in **Edit Vertices**, **Edit Edges** or **Edit Faces**;
3. the selected objects.

Flips wait until you create or cancel the sweep. Each turn or flip of the map is one undo step that follows
**Texture Lock**, and a locked object, or one outside the open group, refuses it with the reason. Turns pivot as quick
turns do in your profile: on the grid point nearest the selection's centre in the TrenchBroom and NetRadiant Custom
profiles while **Snap** is on. The TrenchBroom profile flips about the centre rounded to half the grid, so a selection
that sits on the grid flips in place; set either policy for any profile in **Customize Editor Gestures** >
**Editing**. A custom component pivot always wins. Doom maps take **Rotate 90°** and the flips from the top view, but
not **Roll** or **Pitch**, which would tip the map out of its plane.

To turn or flip from a script, use `map view-transform`, which resolves the same axes for a plan view or a camera;
see [the command line](manual/cli.md).

#### Rotate and scale by exact values

Use **Rotate Selection…** and **Scale Selection…** (the **Edit** menu, the **Transform** menu above the views, the
right-click menu's **Transform** submenu, the **Tools** tab or command search) to type exact values, as NetRadiant Custom's **Arbitrary
Rotation** and **Arbitrary Scale** windows do. In the NetRadiant Custom profile they open with <kbd>Shift</kbd>+<kbd>R</kbd>
and <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd>; **Save Map As** keeps <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>S</kbd>, so
the keys do not clash. Other profiles give them no keys; add your own in **Help** > **Keyboard Shortcuts**.

1. Select the objects.
2. Open the window. **Rotate Selection…** takes **About X**, **About Y** and **About Z** angles in degrees and turns the
   selection about X, then Y, then Z, following the right-hand rule. It opens with 45° about the axis of the plan view
   you used last. **Scale Selection…** takes **X factor**, **Y factor** and **Z factor**: 2 doubles the size along that
   axis, 0.5 halves it, and a negative factor mirrors the selection along it. Brush faces and patches still face
   outward after a mirror, and entity and Doom thing headings reflect as **Flip** does.
3. Choose the **Pivot**: the **Selection centre**, the **World origin**, or **Custom coordinates** that you type.
4. Check the preview. **Lock textures to faces** (rotation) and **Stretch textures with the selection** (scale, the
   **Texture Scale Lock** setting) start from your settings; **Allow Valve 220 conversion** lets a locked texture that
   classic faces cannot hold convert the map's classic faces, keeping how they look.
5. Choose **Apply** to make the change and keep the window open, or **OK** to make it and close the window.

The windows do not block the rest of the studio. After **Apply**, and whenever you change the selection or the map, the
preview rebuilds from the current selection, so pressing **Apply** again turns or scales by the same amount once more.
Each **Apply** is one undo step, and **Repeat Transforms** repeats the whole X, Y and Z turn or the factors. A locked
object, or one outside the open group, refuses the change with the reason, and hidden objects stay where they are. Doom
maps turn about Z only and scale in the top view, so their other fields are off. In **Edit Vertices**, **Edit Edges** or
**Edit Faces** the windows turn or scale the picked components instead, about the pivot you choose or your component
pivot, following **Lock Component UVs** rather than the texture options; with nothing picked they wait and say why.

To do the same from a script, use `map rotate --euler x,y,z` or `map resize --factors x,y,z` with an optional
`--pivot`; see [the command line](manual/cli.md). This workflow is **Partial**: it is tested in offscreen and command-line
checks, not yet with native input on macOS or Linux or in long real-project sessions.

#### Repeat object commands

Use **Edit > Repeat > Repeat Last Commands**, **Tools > Transform**, or the command
palette to replay your recorded object moves, rotations, resizing, shears, mirrors
and ordinary duplications on the current selection. The TrenchBroom profile uses
**Ctrl+R**; **Ctrl+Shift+R** clears the sequence. The NetRadiant Custom profile
gives **Ctrl+R** to **Repeat Transforms** instead (see below). Other profiles leave
these keys unassigned so their existing compiler and editor bindings stay available.

Consecutive commands form a sequence. Changing the selection keeps it available
to repeat, but your next new transform or duplication starts a new sequence.
World pivots and transform origins stay where they were recorded; selecting a
different object does not recentre them. Replay uses the current **Texture Lock**
setting. It affects whole objects, including when component mode is active.

Each replay is one undo step, with progress and cancellation. Clear leaves the map
and its undo history intact; Undo and Redo also clear the repeat sequence. Save As
retains it, while reopening starts a new sequence. Component edits, textures, CSG,
clipping, snapping, prefab insertion and arrays with multiple copies are not
recorded. **Copy Repeat Recipe** exports JSON for the
[`map repeat` CLI workflow](LEVEL_REPEAT.md#automation-and-studio-integration).
This workflow is **Partial**, pending wider real-project and native acceptance.

#### Repeat the last transforms

Use **Repeat Transforms** (**Edit** > **Repeat**, **Tools** > **Transform**, or <kbd>Ctrl</kbd>+<kbd>R</kbd> in the
NetRadiant Custom profile) to build an array the way NetRadiant Custom does. VibeStudio remembers the last move,
rotation, scale (a resize or mirror) and shear you made, each kept until you make another of its kind, whether on
whole objects or on picked vertices, edges and faces. With objects selected, **Repeat Transforms** copies them where
they stand, then scales, shears and turns the copies about their centre and moves them, as one undo step. The copies
stay selected, so each press adds the next step of the array. In **Edit Vertices**, **Edit Edges** or **Edit Faces**
it transforms the picked components instead, about their centre or your component pivot, without copying.

The recorded transforms stay when you change the selection or undo; the status bar shows what was repeated, and the
**History** panel names it. **Reset Repeat Transforms** forgets them without changing the map. Snapping, duplicates and
repeats themselves record nothing. **Repeat Transforms** works on Quake-family maps; on a Doom map, use **Repeat Last
Commands**. To repeat transforms from a script, use `map repeat-transforms`; see [the command line](manual/cli.md). This
workflow is **Partial**: it is tested in offscreen and command-line checks, not yet in long real-project sessions.

#### Clip brushes across views

**Clip Tool** shares its numbered points between every plan and camera view.
Two points define a plane along the originating plan's hidden axis. Most camera
profiles use a vertical plane, with a horizontal fallback for a vertical line.
The NetRadiant Custom profile uses the camera policy described below. A third
point defines a sloped plane. Drag a numbered point to adjust it; switching
panes, projections or layouts preserves the plane. **Backspace** removes the
last point; **Escape** cancels a point drag, then leaves clipping.

New camera points land on visible brush faces. Grid snapping stays on the
face; near a boundary that falls outside the grid, the actual face hit is kept.
By default, double-click a brush face to use its exact source plane;
**Alt+double-click** always provides this action. Plan views snap the
two visible coordinates and preserve the point's hidden coordinate.
**Clip Selection… > Three points** provides labelled XYZ fields for numeric
and keyboard editing; **Preview** closes the dialog and shows the draft.

In the NRC profile, placing the second camera point chooses the dominant view
axis. A segment parallel to that axis uses the points' projected screen direction
to find a usable plane. If the plane would only touch an axial outer face of the
selection, its extrusion changes to cut into the brush. The chosen axis and sign
are stored with the actual points and shared by every view; simply moving the
camera never rotates an established plane. Dragging a point can repair a parallel
or outer-face plane. Cancelling the drag restores both the point and its plane.

**Controls > Customize Gestures… > 3D Camera** exposes **Two-point clip plane
follows the camera with outer-face correction** (`camera.clipViewAxis`). It is
on by default only for NRC; other profiles retain their previous behaviour.
The setting can be changed or exported with the other per-profile gestures.
Three real points and exact face matching are independent of this policy.
Overlapping points along the viewing ray need a sideways move or a third point.

For NRC camera point movement, press a numbered point without modifiers, then
hold **Shift** to move along the strongest XY axis or **Alt** to move along Z.
**Alt+Shift** stays vertical. Movement snaps relative to the point's starting
position, preserving any off-grid offset; exact half-grid ties round to the
nearest even grid step. The press anchor stays fixed when changing modifiers.
Hold **Ctrl**, including Ctrl+Shift or Ctrl+Alt, to return to visible brush-face
snapping. Releasing Shift/Alt also returns to surface picking; a miss retains
the previous point. The movement axis and any refusal appear in the readout.

The same **Customize Gestures… > 3D Camera** form offers **Clip-point movement**
(`camera.clipDragStyle`): `world` is NRC's default, `view` uses the axial plane
nearest the camera view with Alt along its depth axis, and `surface` keeps
surface-only dragging. Other profiles default to `surface`. This setting is
independent of the two-point plane policy. Start a drag without modifiers;
modified initial presses retain the profile's other selection/navigation roles.
VibeStudio's plain first-point drag can still add a second point; modified
movement instead moves the current point. Wider scene-edge/vertex/patch snapping
remains incomplete. **Clip Selection…** supplies numeric XYZ alternatives.
Cancelling, changing the view/grid/profile, or replacing the shared plane
discards an unfinished point drag. Applying the checked cut creates one undo step.

**Tab** cycles back, front and both. **Ctrl+Enter** flips the side;
**Shift+Enter** splits into both pieces. **Enter** waits for validation, then
applies its exact result in one undo step. **Checking cut…**, **Checked cut**
and **Cut refused** report the state. Dashed outlines show the kept result;
the original surfaces remain visible. Plan hatching marks the discarded side
at the first point's depth; use the wire result to inspect an oblique cut.

Brushes wholly on the discarded side are removed, including an emptied brush
entity. Untouched brushes remain selected. Patches and point entities are not
cut. Invalid geometry, a protected object or unsafe source text refuses the
whole operation. New cut faces inherit the brush's most-used material and flags
by default, with the native format's default projection. **Clipper Options…**
can use the **Current material** or a **Custom / caulk material** instead.
Choose a material provided by the project's game assets; `common/caulk` is an
editable example, not a universal game resource. Explicit materials preserve
brush contents but start with zero surface flags and value. Existing faces
retain their source text. Changing the current cut material rechecks the plane
and cancels a queued Apply. A map, selection or save-context change cancels an obsolete draft.
Undo restores geometry, owners, scene membership and selection.

**Quick Clip** starts a single cut and exits after it succeeds. A refused cut
keeps the tool and points available for correction. The NetRadiant Custom
profile also starts quick clipping with **Ctrl+left-click/drag** in a plan view
while brushes are selected and no other drawing/component tool is active.
Remap or disable it using **Mouse and camera controls > Quick clip with left
button** (`plan.quickClipModifiers`). Existing custom selection bindings keep
their priority; explicit conflicting choices are rejected.

**Clipper Options…** saves these preferences across sessions and profiles:

| Option | Behaviour |
| --- | --- |
| **Clear points after a successful cut** | On by default. Turn off to keep the plane for adjustment; undo and selection changes still retire it. |
| **Reset to Back** | Off by default. Reset the kept side after successful cuts and when entering the tool, independently of retaining points. |
| **Use two points in plan views** | Off by default. A click away from the handles starts a fresh plane after two points; cancelling the drag restores the previous plane. Camera and numeric editing still accept three. With three points, a further click away starts a new plane in either view. |
| **Double-click** | Choose no cut (default), Clip or Split. Double-click a numbered point to apply the checked result; Alt+double-click in the camera still matches a face. |
| **Wrap/merge when the plane makes no cut** | Off by default. One point, a degenerate plane, or a plane that neither cuts nor removes anything builds a convex hull around the selected brushes and actual placed points. This fills gaps and concavities. |

Opening the options dialog cancels a queued Apply, including when the dialog is
subsequently cancelled. **Clip Selection…** shows the cut material and links to
the same options. Quick clipping always exits on success, regardless of reset
preferences. These settings work locally with AI disabled.

With **Wrap/merge** enabled, the dashed preview explicitly names the hull
operation. New hull faces use the **current material**, independently of the
cut/caulk setting. Exterior source planes retain their materials, UVs and flags;
when several faces support the same plane, the first in selection order supplies
its surface. The last selected brush keeps its identity, owner and scene group.
Empty source brush entities are removed. Undo restores all sources and selection,
and linked groups update through the usual scene service. A failed or locked
cut never falls back to a hull. An enclosed point on a single brush changes
nothing and is refused without adding history.

Use **Clip Selection… > One point (wrap/merge)** for numeric XYZ entry. Numeric
wrap/merge requires actual points rather than an axis/position plane. Up to 64
brushes in the same face format, 2,048 source faces and 4,096 input points are
supported; the hull must remain within ±32768 units, 128 faces and 256 vertices.
This clipper option wraps whole selected brushes. **Merge Brushes** still
requires an exact convex union and refuses gaps.

The wire preview has a 65,536-segment limit and announces truncation. The edit
still validates the full selection. Cuts use a 0.01-unit side tolerance; brushes
entirely within that tolerance refuse a one-sided cut. Native input and
screen-reader acceptance remain unverified.

The CLI uses the same atomic operation:

```sh
vibestudio --cli map clip maps/start.map --object brush:12 --axis x --at 64 --keep back --output maps/clipped.map
vibestudio --cli map clip maps/start.map --object entity:3 --points "0,0,0 64,64,0 0,0,64" --keep both --output maps/split.map
vibestudio --cli map clip maps/start.map --object brush:12 --axis x --at 64 --cut-texture common/caulk --output maps/caulk-cut.map
vibestudio --cli editor clipper --set material=custom --set customTexture=common/caulk --set resetSide=on --dry-run --json
vibestudio --cli map clip maps/start.map --object brush:12 --points "128,0,64" --wrap-merge --wrap-texture stone/wall --output maps/wrapped.map --json
vibestudio --cli editor clipper --set wrapMerge=on --dry-run --json
vibestudio --cli editor gestures netradiant-custom --set camera.clipDragStyle=view --dry-run --json
vibestudio --cli map clip maps/start.map --object brush:12 --points "0,-48,80 0,48,80" --camera-eye "-200,0,120" --camera-forward "1,0,0" --keep both --output maps/camera-split.map --json
vibestudio --cli map clip maps/start.map --object brush:12 --points "0,-48,80 0,48,80" --view-axis z --view-sign 1 --keep both --output maps/replayed-split.map --json
```

Repeat **--object** for several selections. Choose one plane definition.
Unknown, repeated and incomplete options fail before saving; **--dry-run**
prepares the edit without writing an output. The `clipped` JSON count includes
whole brushes removed; `pieces` contains the remaining selection. An empty
`cutTexture` JSON field means inherited material. **map clip** defaults to
inheritance independently of GUI preferences; **--cut-texture** explicitly
chooses the output material for reproducible scripts.

**--wrap-merge** requires **--wrap-texture** and one to three actual **--points**.
For two points, **--view-axis x|y|z** chooses the plane's extrusion axis (default
z); that synthetic third point never joins the hull. A valid cut or whole-side
removal takes precedence. JSON `operation` is `clip` or `wrap-merge`; `changed`
counts source brushes, `clipped` is zero for a wrap, and `inheritedFaces`,
`newFaces` and `wrapTexture` describe its hull surfaces.

Two-point cuts without wrapping require either **--view-axis x|y|z** or both
**--camera-eye x,y,z** and **--camera-forward x,y,z**. The camera options use the
same NRC policy and selected bounds as the GUI; the forward vector need not be
normalised. **--view-sign 1|-1** requires **--view-axis** and defaults to `1`.
Do not combine explicit axis/sign and camera options. Two-point JSON reports
`planeAxis`, `planeDirection` and `cameraPlane`; the first two can replay the
result without a camera. The synthetic third point still never joins wrap/merge.

#### Make a room with caulk and openings

**Status: Partial.** Draw a brush the size of the room you want, select it and choose **Make Room**
(**Geometry** menu, or the **Brush** section of the **Tools** tab). VibeStudio
builds a wall one grid step thick outside each face and removes the brush, so
its volume becomes the room. The walls fill the outer corners, where NetRadiant
Custom leaves them open. It is one undo step.

As in NetRadiant Custom, two options start on:

- **Caulk the wall faces the room does not see**: every wall face except the
  one facing the room takes the game's caulk material, so the compiler leaves
  it out. That is `common/caulk` in Quake III and `skip` in Quake, the same
  faces the **Caulk** filter hides. Quake II has no caulk material, so the walls
  keep their textures until you name one.
- **Leave picked faces open**: faces you pick in face mode (**Edit Faces**), in
  the camera or with **Select Textured** get no wall, so they become openings
  such as doorways or a skylight. The walls beside an opening stop flush with
  it, and the wall faces that line it keep the open face's material.

To change them, choose **Make Room Options…**. Leave **Caulk material** empty
for the game's own caulk, or name a material your game's assets provide. The
line under the options shows which material Make Room will use on the open map.
**Restore Defaults** brings back NetRadiant Custom's behaviour. The options are
saved with your settings, and `editor make-room` reads or changes them from
the command line.

From the command line, `map hollow --mode room` does the same. Add `--caulk`
for the game's caulk or `--caulk-texture NAME` for your own, and repeat
`--open-face face:brushId:faceNumber` (one-based, as `map select-textured`
prints faces) for each opening. The same options also work with
`--mode overlap` and `--mode touch`:

```text
vibestudio --cli map hollow maps/start.map --object brush:12 --mode room --thickness 16 --caulk --open-face face:12:6 --output maps/start-room.map --json
```

#### Hollow, shrink and expand with the CSG Tool

The **CSG Tool** section of the **Tools** tab, after **Brush**, works like
NetRadiant Custom's CSG Tool window. Choose **CSG Tool** (**Geometry** menu or
the command palette) to open the section with the thickness ready to type. Set
the options once; every button, menu item and key then uses them, and they are
saved with your settings.

- **Thickness**: how thick the walls are, and how far **Shrink Brushes** and
  **Expand Brushes** move each face. **Use Grid** copies the grid size.
- **Faces**: which faces of each brush the tool leaves alone. **All but the
  picked faces** skips the faces you pick in face mode, in the camera or with
  **Select Textured**. **Only the picked faces** works on those alone; a brush
  with none picked is left as it is. **All but those facing the 2D view** skips
  the faces at either end along the active 2D view's axis (in the top view, the
  floor and ceiling), and **All but those facing the camera** does the same
  along the 3D camera's view, so hollowing makes a shaft or a passage.
- **Caulk hidden faces**: hidden faces take the material **Make Room Options…**
  names, or the game's own caulk. The line under the options shows which.
- **Keep inner brush**: keeps the brush instead of removing it. The room is
  then solid, so caulk goes on the faces between the walls and the brush, and
  the outside keeps its materials.

Select brushes, or a brush entity, then choose one of these. Each is one undo
step; a skipped face gets no wall, or stays where it is, and the walls beside
it stop flush with it.

- **Shrink Brushes** and **Expand Brushes** move every face in or out by the
  thickness. Textures stay where they are in the world. A brush a shrink would
  flatten is left as it was, and the status bar says why.
- **Hollow with Diagonal Joints** builds the walls outside each brush, so the
  brush becomes the room. Neighbouring walls meet on a mitred joint, with no
  overlap and no gap.
- **Hollow by Wrapping** builds the walls inside each brush, each stopping at
  the walls before it, like **Hollow Touching…**.
- **Hollow by Extruding Faces** builds a wall straight out from each face, its
  sides square to the face, so the outer edges stay open.
- **Hollow by Pulling Faces** builds the walls outside with the corners filled,
  like **Make Room**.

Hidden brushes, and brushes in locked layers or groups, are left as they are.
From the command line, `map hollow` takes `--mode diagonal`, `--mode extrude`,
`--mode wrap` (the same as `touch`) and `--mode pull` (the same as `room`), with
`--keep-inner`. `--exclude-face face:brushId:faceNumber` and `--only-face`
choose faces as **Faces** does with picked faces, and `--exclude-facing x`,
`y`, `z` or a direction such as `0.7,0.7,0` skips the faces facing it.
`map shrink-brushes` and `map expand-brushes` take the same face options with
`--thickness`:

```text
vibestudio --cli map hollow maps/start.map --object brush:12 --mode diagonal --thickness 16 --exclude-facing z --caulk --output maps/start-shaft.map --json
vibestudio --cli map expand-brushes maps/start.map --object brush:12 --thickness 8 --exclude-face face:12:6 --output maps/start-wide.map --json
```

#### Wrap brushes or picked components

**Wrap/Merge** in **Geometry**, the selection context menu and **Tools > Brush**
creates a convex hull directly. The NetRadiant Custom profile assigns
<kbd>Ctrl</kbd>+<kbd>U</kbd>; other profiles can assign a command key. In object
mode, it replaces selected brushes, fills gaps and retains supporting exterior
surfaces as described above. If the clipper is active, its actual points also
join the hull. The clipper's optional fallback setting does not gate this command.

In **Edit Vertices**, **Edit Edges** or **Edit Faces**, the command wraps only
picked components. Edges contribute both ends, faces contribute their corners,
and patches contribute picked controls. Original brushes, patches and component
picks remain; the new brush joins the object selection. Every new face uses the
current material with default UVs and zero contents/surface flags. No source
surface projection is inherited. Flat or insufficient picks are refused; they
never silently expand to the whole source brush.

The new brush uses the last selected object's native entity and scene group.
With no object selection it follows the last component set; a selected point
entity falls back to worldspawn and the current creation context. Locked groups,
editing contexts and linked groups use the normal scene rules. One undo removes
the new hull from every linked group while restoring the previous selection.

**Wrap/Merge with Points…** adds a preview and up to three exact XYZ points.
Choose **Source objects** or **Wrapped brush** to compare geometry, and change
the new face material before applying. Geometry and available package/staging
materials load on a cancellable worker. Invalid inputs clear the old preview
and disable **Apply Wrap/Merge**. Closing the dialog cancels preparation; changes
to the map, selection, clipper points or package context refuse stale results.

Component wrapping accepts up to 256 component sets, 64 source objects and
4,096 input points, including extra points. The resulting hull has the same
128-face, 256-vertex and ±32768-unit limits as whole-brush wrapping. It remains
a convex brush: concavities and interior points do not become separate geometry.

```sh
vibestudio --cli map wrap-merge maps/start.map --object brush:12 --object brush:13 --texture stone/wall --output maps/wrapped.map --json
vibestudio --cli map wrap-merge maps/start.map --set brush:12/face/4 --point "128,0,192" --texture stone/wall --output maps/component-hull.map --json
vibestudio --cli map wrap-merge maps/start.map --set patch:2/vertex/0,2,4,6,8 --texture stone/wall --output maps/patch-hull.map --dry-run --json
```

Repeat **--object** or **--set**, but do not mix them. Component selectors are
**brush:ID/vertex|edge|face/ID,ID** or **patch:ID/vertex/ID,ID**; IDs come from
**map brush-components** and the zero-based `controlPoints` array in
**map inspect --json**. Repeat **--point x,y,z** up to three times.
**--texture** and **--output** are required; **--dry-run** checks
without writing. JSON reports `sourcesRetained`, `brushId`, `sourceBrushes`,
`sourcePatches`, `vertices`, `faces`, `inheritedFaces` and `newFaces`.

#### Move components in the camera

**Edit Vertices**, **Edit Edges** and **Edit Faces** also show handles in the
3D camera. They share picks with the plan views and keep a camera-only layout
in 3D. Selected patches show their control points in every component mode.

Click a handle to select it; use the profile's toggle modifier to add or remove
it. In component mode, the other Ctrl/Shift key adds without removing existing
picks; holding both subtracts. A profile with no toggle modifier uses Ctrl.
For custom chords, an exact configured toggle/add role takes precedence over
the combined-key subtraction alternative.
Drag a rectangle from empty space to select several handles. The TrenchBroom
profile toggles each enclosed handle with a plain lasso and adds with Ctrl;
other profiles replace with a plain lasso and toggle with their toggle key.
**Customize Editor Gestures…** can change this through **Plain component lasso
toggles; toggle modifier adds**, separately for plan and camera views.

In the camera, a click selects every available handle with the same solved
vertex, edge or face geometry. Edge endpoints and face corners must match;
crossing edges or different faces sharing a centre remain separate. This keeps
shared terrain corners together. Plan views select the handles stacked at that
screen position, including different depths. Patch control points can share a
corner with other selected geometry.
The camera picks visible handles by default. **X-ray Component Handles** in
**Geometry** or **Tools > Vertices** includes handles behind surfaces.
**Select All** (or **Ctrl+A**) selects all available handles of the selected
objects, including occluded ones. **Select None** clears component picks;
**Invert Selection** reverses them. These commands preserve the object
selection and component mode, create no geometry history, and cancel pending
edits. Locked/closed-group objects do not expose component handles.

To distinguish overlapping depths, hold **Alt** and scroll over a component
handle: wheel down steps farther, wheel up steps nearer, wrapping at either
end. **Tab** and **Shift+Tab** cycle forwards/backwards at the last component
pick or wheel position in the focused viewport. With no previous position,
they start at the first on-screen picked handle, or the first available handle
if none is picked. **Next/Previous Overlapping Component** in **Geometry >
Component Selection** and **Tools > Component Selection** provide the same
commands for keyboard customisation. Menus retain the last focused view.

Cycling replaces only handles in the small hit area and preserves picks
elsewhere. Coincident geometry stays together; equal-centre crossing edges
and different faces remain separate. If several depths are already picked,
forward cycling starts with the nearest and reverse cycling with the farthest.
The status and accessible description give the overlap number and XYZ position.
Starting a plan drag on a picked depth preserves that single-depth selection.
For plan ordering, near means higher Z in Top, lower Y in Front and higher X in
Side. Camera ordering follows its view direction; **X-ray Component Handles**
controls whether occluded handles participate. Handles behind the camera or
outside the view are excluded even with X-ray enabled.

**Customize Editor Gestures… > Cycle overlapping components with wheel (None
disables)** changes the chord independently for plans and camera. Plain wheel
navigation stays available. Fractional wheel motion accumulates to a notch;
moving to another hit area or changing the view resets it. A truncated handle
context refuses cycling. Wheel/key cycling waits until an active edit ends;
the named commands cancel a pending component edit first. These are VibeStudio
extensions; exact native TB/NRC input parity remains unverified.

Switching between **Edit Vertices**, **Edit Edges** and **Edit Faces** converts
the current picks. A picked face becomes its boundary edges or corners; a
picked edge becomes its endpoints. Converting upwards requires a complete
boundary: both endpoints for an edge, every corner or every boundary edge for
a face. Three sides of a quad do not select that face even though all four
corners are covered. Patch control-point picks survive every mode switch.
Choosing the already active mode still leaves component editing.

**Geometry > Component Selection** and **Tools > Component Selection** provide:

- **Select Touching Edges/Faces**: convert using any incident pick, instead of
  requiring a complete boundary.
- **Grow Component Selection**: add one neighbour step. Vertices connect along
  edges, edges at shared endpoints, and faces across shared boundary edges.
- **Shrink Component Selection**: remove one boundary ring. A wholly selected
  closed brush stays selected because it has no unselected neighbour.
- **Select Connected Components**: follow those neighbours to select the whole
  connected part of each object containing a pick.

Patch points use four neighbours along rows and columns, without diagonals or
wrapping. Shrink also removes the patch's open outer border. These operations
work independently inside each picked brush or patch; they do not jump into
other objects at coincident positions. They preserve object selection and
geometry history and cancel pending edits. Visibility affects which objects
expose handles, but camera occlusion does not prevent topology selection from
reaching a back-facing component. If the handle limit would truncate a
conversion, the command leaves the current mode and picks intact and asks for
fewer selected objects.

The read-only `map select-components` query uses the same rules:

```sh
vibestudio --cli map select-components room.map --set brush:6/face/4 --operation convert --to vertex --json
vibestudio --cli map select-components room.map --set brush:6/vertex/0 --operation convert --to face --coverage touching --json
vibestudio --cli map select-components room.map --set patch:0/vertex/12 --operation grow --steps 2 --json
```

Repeat `--set` for multiple objects. One object must use one source kind;
repeated sets of that kind are merged. `--coverage complete` is the conversion
default. Grow and Shrink accept `--steps 1..256`; `connected` follows the whole
graph. Patch selectors always use `vertex`. JSON includes sorted `sets`,
counts and `selectors` that can be passed as `--set` values to
`map transform-components`. Empty results are valid. The query writes no map
and accepts no `--output`; explicit CLI IDs inspect objects independently of
GUI visibility. Missing/stale IDs fail the whole query. Re-query after any
topology-changing edit.

Closing a map or opening a different one clears its component mode, handles
and picks. Viewport selection commands require an active component mode.

Drag the labelled **X**, **Y** or **Z** control to move along that world axis.
Drag the centre or a selected handle to move in the camera plane. The current
grid snaps the movement distance and preserves spacing between picked handles.
A dashed outline previews the rebuilt brush or patch control grid. **Checking
geometry…** means validation is still running; releasing waits for that result.
Invalid geometry or a locked object leaves the whole selection unchanged.
A valid release creates one map undo step, including across several brushes.

**Esc** cancels a drag, then clears picks, then leaves component mode. A changed
document, selection, camera, grid or tool also cancels a pending drag. Navigation
remains available between edits. **Edit Brush Components…** and **Edit Patch
Control Points…** provide numeric, keyboard-accessible editing. **Remove Selected
Components** and **Delete Selection** use the shared camera/plan picks.

Handle display is bounded to 128 objects and 16,384 handles; a limit message
asks you to select fewer objects. The preview is a wire outline over the original
surface. Numeric and direct rotation, scale and shear controls are described
below. Arbitrary point insertion gestures and full native profile acceptance
remain incomplete. VibeStudio is still pre-alpha.

#### Transform picked components

With **Edit Vertices**, **Edit Edges** or **Edit Faces** active, pick handles and
choose **Transform Components…** from **Geometry** or **Tools > Vertices**.
Choose **Move**, **Rotate**, **Scale** or **Shear**, then enter the values.
Rotation angles are degrees; shear is displacement along one axis per unit
along a different axis. Scale accepts negative factors for reflection.
The default pivot is the centre of the picked corners' combined bounds, across
all affected brushes and patches. **Use a custom pivot** supplies exact XYZ.
**Grid** snaps the resulting corner positions; **No snapping** preserves the
numeric transform. Coincident corners merge only with **Allow coincident
vertices to merge**; every brush must remain a three-dimensional convex solid.

**Rotate 90° Left/Right**, **Rotate Selection…**, **Scale Selection…**,
**Resize Selection…**, **Shear…**, mirroring and grid snapping target picked
components while component mode is active. Resize opens component scale
controls; **Rotate Selection…** turns the picks about X, Y and Z at once.
No picked handles means no edit. Surviving handles stay selected after a
successful transform. Brushes and patch control points share one atomic undo
step; a locked object, invalid solid or changed selection cancels the batch.
The progress window supports cancellation without publishing a partial edit.

**Snap Picked Vertex…** in **Geometry** or **Tools > Vertices** moves one
geometric brush vertex to another existing vertex. Select the source, open the
command and choose a target by brush/vertex ID and XYZ. Targets come from the
editable brushes whose component handles are shown. Coincident source vertices
in several brushes can move together; different source positions are refused.
The TrenchBroom profile also offers **Alt+Shift+click** on the target in any
view. Use overlap cycling first when a plan pick includes different depths.
The exact target bypasses grid rounding and permits coincident-corner merging,
while convexity, scene locks and **Lock Component UVs** still apply. A refused
merge leaves the map untouched; a successful snap has one undo step. Choosing
the source itself changes nothing. Patch points are not quick-snap targets.

Component movement and insertion show labelled X/Y/Z distance segments.
The constrained segment is thicker; a dashed trace means geometry is still
being checked. Axis letters and distances remain visible in high-contrast
themes. Numeric component movement accepts displacements up to ±65536 units
so corners can cross the valid world; resulting corners and pivots remain
bounded by ±32768 units.
In plan views the selected object's name returns when the component drag or
insertion ends, keeping the active distance readout clear.

**Lock Component UVs** is separate from object **Texture Lock** and defaults
off; the TrenchBroom profile toggles it with <kbd>U</kbd>. It applies to
ordinary component drags, numeric transforms and **Round Selection to Grid**,
preserving the original texture coordinates at corresponding brush corners.
Patch control point UV values stay attached to their points.

Some deformations cannot keep every corner's coordinates in one planar face
projection: drag a corner so a side becomes a trapezoid, for example. With
**Fit UVs When Lock Fails** on (the default), that face keeps the coordinates
of three of its corners, preferring corners the edit did not move, and the
other corners drift, as NetRadiant Custom's texture vertex lock does. The status
bar says how many faces were fitted. A classic face that cannot show the fitted
mapping takes the nearest classic mapping instead (one rotation and two scales),
unless **Allow map-wide Valve 220 conversion** is on. Turn **Fit UVs When Lock
Fails** off to refuse such edits instead. Both commands are in **Geometry** and
**Tools > Vertices**, and **Transform Components…** offers the fit for one
transform.

**Allow map-wide Valve 220 conversion** permits classic maps to change mapping
syntax when necessary; all classic faces convert together, preserving untouched
geometry and UVs. Undo restores the dialect. Topology insertion/splitting/removal
and face-plane extrusion use their own documented mapping policies.

Choose **Rotate Components**, **Scale Components** or **Shear Components** in
**Geometry** or the Tools tab to manipulate picked components directly in a
plan or camera view. Drag a labelled rotation ring, an XYZ scale handle or
**All** for uniform scale. Shear labels name the changed axis followed by the
axis that determines the offset: **X/Y** changes X in proportion to Y. Rotate
rings support multiple full turns. Scale can pass through zero into a
reflection, but a flat brush cannot be applied. Use **Move Components** to
return to ordinary dragging. Split and extrusion tools switch back to movement.

**Move Component Pivot** provides XYZ and view-plane pivot handles without
editing geometry or adding undo steps. The custom pivot is shared across views
and numeric component transforms. **Reset Component Pivot** restores the picked
corners' combined bounds centre. A custom pivot lasts until reset, leaving
component mode or opening another map.

**Component Manipulator Options…** provides exact pivot coordinates and
persistent rotation, scale and shear steps. Defaults are 15°, 0.125 and 0.125;
**No snapping** disables a step. Scale snaps factors relative to 1. Pivot
movement uses the current map grid. Direct affine gestures use these angle and
factor steps, without snapping final corner coordinates or merging corners;
use **Transform Components…** for final-grid snapping or an explicit merge.

The dashed preview shows checked geometry. **Checking geometry…** keeps a
release pending until the current result arrives. **Esc**, changing picks or
changing tools cancels it. A rejected result leaves the map untouched; a valid
release creates one undo step. **Transform Components…** and the options dialog
provide keyboard-accessible numeric alternatives to dragging.

For automation, inspect component IDs with `map brush-components`. Patch
control-point IDs are zero-based `row * width + column`. Repeat `--set` once per object, with up to
256 objects and 4,096 component IDs per object:

```sh
vibestudio --cli map transform-components room.map --set brush:6/vertex/0,1,2,3,4,5,6,7 --operation rotate --axis z --angle 15 --pivot 0,0,0 --uv-lock on --allow-valve220 --output rotated.map
vibestudio --cli map transform-components room.map --set brush:6/face/4 --set patch:0/vertex/0,1,2 --operation move --delta 0,0,8 --output reshaped.map
vibestudio --cli map transform-components room.map --set brush:6/vertex/0 --operation snap --target brush:6/vertex/1 --output snapped.map
```

`move` requires `--delta x,y,z`; `rotate` requires `--axis x|y|z` and `--angle`;
`scale` requires `--factors x,y,z`; `shear` requires distinct `--axis` and
`--along` axes and a `--factor`. Optional `--grid`, `--allow-collapse`,
`--uv-lock off|on|fit` (default off), `--dry-run` and `--overwrite` share the
editor's validation and save service. `on` refuses a deformation the exact lock
cannot hold; `fit` keeps three corners instead, and the JSON result counts the
`fittedFaces`. `--allow-valve220` requires UV lock on or fit. Component IDs
describe the current topology; inspect again after editing it.

`snap` requires one `--target brush:ID/vertex/ID` and brush-vertex source sets
at one geometric position. It computes the exact displacement and enables
merging automatically. `--grid`, `--pivot` and `--delta` are refused for this
operation. Dry runs report the target, displacement and collapsed-corner count;
UV policy, locked-object checks and protected output paths are shared with the
other component transforms. The CLI may name any existing brush vertex target.

#### Transform independently picked faces

**Status: Partial.** Faces picked by **Select Textured** can be moved, rotated,
scaled and sheared without selecting their brushes, as in NetRadiant Custom's
face mode:

1. Choose a material and run **Select Textured**.
2. Turn on **Edit Faces**. The status bar says how many picked faces join the
   edit; no brush needs to be selected.
3. Press the arrow keys in a plan view (one grid step, or eight with
   **Shift**), drag with **Rotate Components**, **Scale Components** or
   **Shear Components**, or choose **Transform Components…**. The dialog states
   that it includes faces picked by **Select Textured**.

Face handles you pick on selected brushes move in the same edit, and the
default pivot is the centre of everything picked. The edit uses the shared
component transform: the picked faces' corners move, each brush is rebuilt as a
convex solid, **Lock Component UVs** applies, and the whole edit is one undo
step. Afterwards each pick stays on the face that carries its moved plane; a
face the rebuild removes is dropped, and the status bar says how many picks
remain. **Undo** and **Redo** restore both the faces and their picks.

The edit is refused, leaving the map unchanged, when a picked brush is hidden,
filtered, locked, inside a closed group or changed since the query, or when the
picks span more than 256 brushes. **Edit Vertices** and **Edit Edges** leave
independent faces out, and splitting, extrusion and vertex insertion use handles
only. Every operation moves the face's corners, as TrenchBroom's face tool does
when it moves a face; NetRadiant Custom rotates and scales the face plane
instead, which can give a different shape when the neighbouring faces are not
perpendicular.

From the command line, pass the returned face targets with `--face`:

```sh
vibestudio --cli map select-textured room.map --material stone --json
vibestudio --cli map transform-components room.map --face face:6:5 --operation rotate --axis z --angle 15 --uv-lock on --allow-valve220 --output turned.map
```

See [Move, rotate and scale textured faces](manual/cli.md#move-rotate-and-scale-textured-faces)
for the options and the JSON fields.

#### Add and remove brush corners

Choose **Geometry > Insert Vertex** to add a corner directly in a plan view or
the camera. Point at an edge or face of a selected brush: a diamond with a plus
marks the proposed grid point. Drag it outward until the dashed, checked hull
appears, then release. The point can be anywhere along the edge or face, rather
than only its midpoint. Existing vertex handles keep their selection behaviour.
The TrenchBroom profile also offers **Shift+drag** while **Edit Vertices** is
active. Movement constraints and grid snapping follow your
[component drag profile](manual/editor-profiles.md#move-and-insert-components). With the
TrenchBroom defaults, camera drags stay horizontal, **Alt** moves vertically,
and **Ctrl** uses absolute grid coordinates. Plan views ignore Alt. Press
**Shift** during movement to constrain its dominant axis; the Shift used to
start insertion does not initially constrain it. Other profiles retain
view-plane insertion with an optional Alt Z constraint. Use a front or side
view to raise an edge that is flat in the top view.

Coincident edges or faces of selected brushes are edited together. Each
original corner must survive; an interior, flat, locked or invalid result
refuses the entire edit. **Checking geometry…** means validation is still
running. Releasing waits for the latest result; **Esc**, changing the selection
or losing focus cancels an unfinished drag. Success selects the new
vertices across all panes and creates one undo step. Turn **Insert Vertex** off
or choose **Move Components** to return to ordinary movement. Camera insertion
respects **X-ray Component Handles** and the current visible surfaces.

**Edit Brush Components…** lets you change a brush's topology with a preview.
Enter **New vertex X**, **Y** and **Z**, then choose **Insert Vertex**. The grid
snaps these coordinates. Place the point outside an existing face or edge to
make a new corner; points inside the brush or on its flat surfaces cannot be
stored as additional corners in a brush map. The new corner becomes selected.

Choose vertices, edges or faces, then **Remove Selected Components**. Removing
an edge or face removes all its vertices and rebuilds the solid from those
left. A result that is flat or otherwise invalid is refused. **Undo** restores
the geometry and selection; **Apply** commits the draft as one map edit.
Materials, flags and stored texture parameters are retained, but a deformed
face does not have texture lock.

To split an edge or face in the main views, choose **Edit Edges** or **Edit
Faces**, pick its handles, and turn on **Geometry > Split Edge/Face**. Drag a
handle in a plan view or use the camera's XYZ/centre controls. Each picked
edge midpoint or face centre becomes a new corner at the dragged position.
The dashed outline shows the checked convex result. Release applies it as
one undo step; **Esc** cancels, including while **Checking geometry…** is shown.
The new vertices are selected in every pane so you can continue shaping them.
Turn **Split Edge/Face** off to resume ordinary component movement.

Splitting preserves the original corners. A movement into the brush, a point
that remains flat, a locked object or any invalid result refuses the whole
operation. Use **Edit Brush Components… > Split Selected Edges/Faces** with
the XYZ movement fields for keyboard and numeric editing. The dialog's
**Allow Vertex Collapse** also permits old corners to disappear when needed;
every requested new corner must still survive. **Apply** commits its draft.
For automation, repeat `--brush` in `map insert-vertices` to apply the same
positions to several brushes atomically:

```sh
vibestudio --cli map insert-vertices room.map --brush 0 --brush 1 --position -32,8,16 --output reshaped.map
```

Patches use **Patch Rows and Columns** instead. Deformation texture lock for
these corner edits remains incomplete. Use the dedicated extrusion tool below
to move a brush plane along its normal.

In the plan views or camera, pick handles with **Edit Vertices**, **Edit Edges** or
**Edit Faces**, then use **Geometry > Remove Selected Components** or
**Delete Selection**. Removing an edge or face removes its incident vertices
and rebuilds the convex solid. All affected brushes change as one undo step.
A locked or invalid brush refuses the entire edit. Preparation runs in the
background; **Cancel** leaves geometry, history and component picks unchanged.
Changing the map, selection, tool or profile while preparing also discards the
result. Undo and redo restore the geometry exactly. Removed brush handles are
cleared so their IDs cannot select different corners after rebuilding.

**Delete Selection** follows the active profile. In **NetRadiant Custom**,
**Delete**, **Backspace** and **Z** remove picked brush components. If component
mode has no picks, they delete the selected objects instead. Picked patch points
are kept while picked brush components are removed; patch points alone never
fall back to deleting objects. **TrenchBroom** and the default profile require
picked components while a component tool is active. **Remove Selected
Components** always requires brush component picks, in every profile.

Individual patch control points cannot be removed; use **Patch Rows and
Columns**. In profiles other than NRC, deselect patch points before removing
brush components. Finish active draw, sweep, drag or surface-edit work before
deleting geometry. In TB clip mode, the shared **Delete Selection** command
removes the last clip point without changing map history. Other profiles keep
geometry deletion disabled during clipping; the viewport's existing
**Delete**/**Backspace** clip-point controls remain available.

#### Extrude brush faces

Choose **Geometry > Extrude Faces**, then pick face handles. Drag the camera's
**N** axis or the picked handle in a plan view. The face moves along its outward
normal; adjacent planes stay fixed. Positive distances expand the brush and
negative distances contract it. If the normal points directly into the view,
use another view or **Extrude Faces Precisely…**.

**Split Extrusion** makes a separate brush. Outward extrusion keeps the original
brush and adds the new section; inward extrusion partitions the original brush
at the moved plane. The moved face remains selected in every pane. Turn
**Extrude Faces** off to resume ordinary face component movement.

The dashed preview shows the checked result. Release applies it as one undo
step, including the other linked groups; **Esc** cancels a pending drag. Locked or
invalid geometry refuses the whole edit. Extrusion stops before an original
face disappears. Select one face per brush, up to 128 brushes, with parallel
normals. Opposite normals can move together along the reference face's
direction, but cannot split together. The first selected brush in document
order supplies that reference; **N** and the numeric dialog identify it.
Split sections must be at least 0.02
units thick.

Dragging snaps the distance to grid steps or a face vertex to a grid plane,
whichever needs the smaller adjustment. **Extrude Faces Precisely…** accepts an
exact signed distance and a **Split into separate brushes** checkbox; numeric
distances are not snapped. The current **Texture Lock** setting controls the
moved face's projection. Both split caps retain matching world texture
projection, and materials, surface flags, entity ownership and groups survive
the edit. Other component moves and transforms use the separate
[component UV lock](manual/levels.md#transform-picked-components).

For automation, inspect face IDs with `map brush-components`, then run:

```text
vibestudio map extrude-faces room.map --face 0:5 --distance 16 --split --output room-extruded.map
```

Repeat `--face brush:face` for more brushes; the first option supplies the
reference face. `--grid` enables the same snapping;
`--no-texture-lock` keeps the original world projection. Standard `--dry-run`
and overwrite protections apply. JSON reports changed/created counts and active
faces. An extruded brush keeps its place in the file, but a split adds a new
brush at the end of its entity, so inspect IDs again after saving and reopening
a split (see [Save your work](manual/levels.md#save-your-work)). These tools are available, but full TB/NRC
gesture parity and real-project acceptance remain open; VibeStudio is pre-alpha.

#### Sweep selected faces

In **Edit Faces**, pick one or more brush faces and choose **Geometry > Sweep
Faces…** or **Tools > Brush > Sweep Faces…**. Set the destination translation,
rotation axis and angle, and XYZ scale. **Create Sweep** adds convex sections
between the source and destination, selects the new brushes and records one
undo step. The original brushes remain. Each profile keeps its source entity
and group; linked groups update through the normal scene service.

For direct placement, choose **Sweep Tool** in the same menu or tool shelf
(<kbd>Y</kbd> in the TrenchBroom profile). A compact floating palette shares one
destination with all plan and camera views. Drag **Move** to place the cap,
a labelled rotation ring to turn it, or **Scale** to change its uniform size.
Plans show the ring perpendicular to their view; the camera offers all three
world axes. Successive rotations compose around the destination centre.
The numeric translation, rotation and scale controls stay synchronised.

Movement follows the current grid: in a plan it stays in that plan, and in the
perspective camera it starts on the world XY plane. Hold <kbd>Alt</kbd> to move
vertically in the camera. During movement, <kbd>Shift</kbd> constrains the
strongest axis and <kbd>Ctrl</kbd> snaps to absolute world-grid coordinates.
Start the centre drag with no modifiers or with <kbd>Alt</kbd>; rotation and
scale handles require a plain press. Scale follows the rotated source arm,
snapping to the closest grid-plane crossing; it clamps to 0.01 instead of
reflecting the profile. **Decrease Scale** and **Increase Scale** move that arm
by one grid step, or one world unit with snapping off. Rotation uses the angle
step in **Component Manipulator Options**.

Releasing a handle retains the draft. **Create Sweep**, or <kbd>Enter</kbd> while
a level view has focus, applies the latest checked result. In a level view,
<kbd>[</kbd> and <kbd>]</kbd> change the sweep scale while the tool is active.
Custom shortcuts for these three named commands replace their local defaults;
an explicitly unbound command has no local default. <kbd>Escape</kbd> cancels
the current drag, then resets a changed destination, then leaves an unchanged
tool. Closing the palette or toggling **Sweep Tool** cancels the draft directly.
Changing the grid during a drag cancels that gesture.

**Arc** follows a circular bend with any translation along the rotation axis
added as a rise. **Straight** moves the cap centre along a straight line while
rotating and scaling the profile. **S-bend** follows the combined source normal
and its rotated destination direction. The rotation uses the shortest turn:
330° is −30°, and 360° is unchanged. Opposing source normals cancel; S-bend then
uses a straight centre path. Translation and scale use world axes about the
centre of the combined picked-face bounds.

Choose 1–64 **Segments** and 1–8 **Iterations**; defaults are eight segments and
one iteration. Each iteration continues the previous destination transform,
including its rotation and scale. **Round generated stations to whole units**
starts on. The first source station stays exact, even off-grid; turn rounding
off for fractional stations. A collapsed or out-of-world section is omitted
and listed with its brush, face, iteration and segment. If no valid sections
remain, creation is disabled. Inspect these details before applying.

The preview offers **Sweep and source**, **Swept brushes** and **Source objects**,
using available project/package material images. New faces use **Material** and
default mapping/flags; original surfaces stay unchanged. Changing controls
invalidates the old preview immediately. Closing the dialog cancels preparation;
a changed document, package or face selection prevents a stale result applying.
Nothing is saved until the normal map save operation.

The main views show destination outlines and checked section edges. Pending
caps use dashes; **Show textured preview** expands the palette to inspect the
material-aware source/result preview. A changed map, selection, scene context,
package or editor shortcut/profile closes the viewport tool. Source geometry
is retained until explicit creation, and a stale draft cannot overwrite newer
work.

The tool accepts up to 64 distinct faces and 4096 attempted sections, with
coordinates inside ±32768 units and scale components of at least 0.01. This
workflow is **Partial**: textured section previews within the main camera,
dense production maps, complete TB gesture parity and native platform/input
acceptance remain open.

Inspect face IDs with `map brush-components`, then use the same service through:

```text
vibestudio --cli map sweep maps/start.map --set brush:12/face/4 --delta "64,64,32" --axis "0,0,1" --angle 90 --path arc --segments 8 --texture stone/wall --output maps/swept.map --json
```

Repeat **--set brush:ID/face/IDs** for additional profiles. **--scale x,y,z**,
**--iterations**, **--alignment integer|free** and **--path arc|straight|s-bend**
match the dialog. Standard **--dry-run** and overwrite protections apply. JSON
reports attempted/created counts, new brush IDs and every skipped section;
text output also lists omissions. Reinspect IDs after saving and reopening,
because native map source order can renumber them.

#### Place entities from the camera

Aim at a brush or patch, select a point class on the **Entities** tab, then choose **Place at Camera Surface**.
A small centre reticle appears when camera placement is ready.
The entity uses the surface at the camera centre, the current grid and the active **Create in** layer.
Its definition bounds stay outside the surface; classes without bounds use an 8-unit marker.
**Clearance** adds a gap. Sloped surfaces can leave a slightly larger gap after grid snapping.
This checks the sampled surface, not collisions against the rest of the map.

Placement is one undo step and uses the same save, recovery and scene-lock checks as plan creation.
Wait for the camera to finish rendering and complete any active drawing or transform before placing.
In Doom and Hexen maps, select a thing type and aim at a sector floor. Things use native whole-unit
coordinates; clearance is ignored and a snapped point outside the visible floor is refused.
New UDMF thing placement remains planned.

#### Draw brushes from the camera

Choose **Draw Brush** in the camera tool list. To build from an existing floor or wall, aim the centre crosshair at
it and choose **Use Camera Surface**. The construction grid moves to that point and **Direction** chooses the side
towards the camera. A sloped face supplies the nearest X/Y, X/Z or Y/Z plane; this does not create a slanted work plane.
If the view is still rendering or there is no reachable surface, the readout explains why the previous plane stays in use.

You can also choose a plane and enter **Base**, **Depth** and **Direction** yourself. **Use Work Zone** takes the last
selection's depth range, or 0–64 in an empty map. Drag a footprint, use the wheel to change depth, then release to create
one brush. **Positive (+)** and **Negative (−)** extend from the plane along its hidden axis. **Numeric Brush…** uses
the same plane, direction and material for exact bounds or another primitive shape.

The dashed draft appears in the camera and plan views. <kbd>Esc</kbd> or **Cancel Draft** discards it; release commits
one undo step. Changing the camera, plane, direction, grid, selection, material or tool also cancels a draft. Choosing
**Use Camera Surface** discards any current draft before sampling the new plane.

#### Build with shapes

The **Shapes** tab chooses what brush drawing makes, as the object bar does in Hammer and J.A.C.K.: **Box**,
**Wedge**, **Cylinder**, **Cone** and **Sphere** are one brush each, and **Arch**, **Ring**, **Stairs** and **Room** are
several. A shape fills the box you draw, in the map's own face format and the material chosen on the **Textures**
tab, and is one undo step that selects its brushes.

On a Quake III map the tab also offers patch shapes, as Radiant's Patch menu makes them: **Patch Plane**, **Patch
Cylinder**, **Square Patch Cylinder**, **Patch Cone**, **Patch Sphere**, **Patch Bevel** and **Patch End Cap**. Each is
one patch filling the box; **Segments** sets how many spans go round a cylinder, cone or sphere.

1. Choose a shape. Its settings appear below it; the box has none.
2. Choose **Draw in a View**, then drag a box in a 2D view or the 3D view. **Add at View Centre** puts one in the
   middle of the active 2D view instead.
3. To turn existing brushes into the shape, select them and choose **Replace Selected Brushes**: the shape fills their
   bounds, as the Radiant editors' arbitrary-sided brush commands do. Brushes of a brush entity are left alone. To
   rebuild each brush in its own bounds instead, brush entities included, see
   [Rebuild brushes as prisms, cones and spheres](manual/levels.md#rebuild-brushes-as-prisms-cones-and-spheres).

| Setting | What it does |
| --- | --- |
| **Axis** | The axis a cylinder or cone runs along, a sphere's poles, a wedge's height, and what an arch or ring turns about. **Across the View** follows the view you draw in, so an arch drawn in the **Front** view stands up as a doorway. |
| **Sides** or **Segments** | How round a cylinder, cone or sphere is, or how many brushes make an arch or ring. A cylinder takes up to 126 sides and a cone up to 127, the most a brush's 128 faces allow; a sphere's sides times **Bands** is at most 128. |
| **Fit** | How a cylinder, cone or sphere sits in the box. **Fill the Box** stretches it to every side of the box. **Round on the Larger Side** builds it as NetRadiant Custom does: round on the box's larger side with a face square to the first axis across the view, so a four-sided cylinder is a square and an eight-sided one keeps the box's own faces; a sphere is a true sphere on the largest side. Such a shape can overhang the box's shorter sides. |
| **Walls** | How thick an arch, ring or room is. Walls as thick as an arch is wide close it into slices. |
| **Sweep** and **Start** | How far round an arch goes, and where it starts, in degrees from the view's right towards its top. |
| **Steps** and **Climbs** | How many solid steps stairs have, and which way they rise: along the longer side, or towards +X, −X, +Y or −Y. |

On a Doom map the tab offers sector shapes instead: **Rectangle** and **Polygon** make one sector, **Stairs** a row
of step sectors each **Step height** above the last, as Doom Builder's stair builder does, and **Grid** the footprint
cut into **Columns** and **Rows**. Choose **Draw in a View** and drag a box in the **Top** view, inside a room or
out in the void, and the shape fills it, as Doom Builder's rectangle and ellipse modes do; keep dragging boxes for
more, and press <kbd>Escape</kbd> to stop. **Add at View Centre** adds one of the **Width** and **Depth** you set in the
middle of the view instead. For a sector of any outline, use **Draw Sector** on the tool bar. Rebuild the nodes
before playing.

#### Rebuild brushes as prisms, cones and spheres

**Partial:** NetRadiant Custom's Brush menu shapes, on Quake-family maps. Select brushes, or a brush entity, and choose
**Make Prism…**, **Make Cone…** or **Make Sphere…** (**Edit** > **Brush Shapes**, the **Geometry** menu above the
views, the **Brush Shapes** section of the **Tools** tab, or command search), then give the number of sides. Each brush
is rebuilt in place within its own bounds, round on its larger side with a face square to the first axis, in the
material chosen on the **Textures** tab:

- a prism or cone runs along the active 2D view's depth axis: Z in **Top**, Y in **Front** and X in **Side**. A cone's
  base is at the low end and its point over the middle of the high end.
- a sphere keeps its poles on Z, with as many latitude bands as sides, up to 11 of each.

**Make 3-Sided Prism** to **Make 9-Sided Prism** do the same without asking. In the NetRadiant Custom profile they are
<kbd>Ctrl</kbd>+<kbd>3</kbd> to <kbd>Ctrl</kbd>+<kbd>9</kbd> while a 2D view or the camera has the keyboard and a brush
is selected; anywhere else, or with nothing selected, those keys still switch work modes.

The brushes keep their numbers, their brush entity, layer or group and their place in the file, and the selection stays
as it was. Their faces get a fresh default alignment and keep the brush's content flags. The whole change is one undo
step. Hidden brushes, and brushes too small for that many sides, are left as they were and named in the status bar; a
brush on a locked layer, or outside the open group, stops the change. `map add-shape --in-place --fit circle` does the
same from the [command line](manual/cli.md).

#### Create a brush from typed points

**Partial:** TrenchBroom's debug **Create Brush** as an ordinary command, on Quake-family maps. Choose **Create Brush
from Points…** (**Edit** > **Brush Shapes**, the **Create** menu above the views, the **Brush Shapes** section of the
**Tools** tab, or command search) and type four or more points. The new brush is the convex solid that encloses them.

1. Type the points in **Points**, in any of these forms. Each line can use a different one, and text after `//` is
   ignored:
   - `x,y,z` groups separated by spaces or semicolons: `0,0,0 128,0,0 0,128,0 0,0,64`
   - one or more `x y z` points per line
   - TrenchBroom's bracketed form: `(0 0 0) (128 0 0) (0 128 0) (0 0 64)`
2. **Add Selected Corners** appends the corners of the selected brushes, or of the picked faces, edges and vertices,
   in the same form [Copy Vertex Coordinates](manual/levels.md#copy-vertex-coordinates) writes.
3. Check the material in **Material**; it starts as the one chosen on the **Textures** tab.
4. Watch the status line and the preview. They say how many corners and faces the brush will have, and how many
   points add no corner because they lie inside the brush or on its faces. Points all in one place, on one line or on
   one plane cannot enclose a volume and are refused, as are more than 4,096 points or a result with more than 128
   faces or 256 corners.
5. Choose **Create Brush**.

The brush goes to worldspawn in the map's own face format, into the open group or the active layer, and is selected.
It is one undo step; a locked destination stops it, and a linked group's copies get it too. The dialog remembers the
last points you typed. `map add-brush --points` does the same from the [command line](manual/cli.md).

#### Assemble a brush from clicked points

**Partial:** TrenchBroom's **Brush Tool**, on Quake-family maps. Choose **Assemble Brush Tool** (<kbd>B</kbd> in the
TrenchBroom profile; **Edit** > **Brush Shapes**, the **Create** menu above the views, the **Brush Shapes** section of
the **Tools** tab, or command search). The 2D views and the camera share one set of points, drawn as squares, with the
outline or brush they make. While the tool is on, clicks add points instead of selecting.

In the camera:

- Click a brush face to add a point on the grid in that face's plane. Where no face is under the pointer, the point
  goes on the construction plane that **Draw Brush** uses, the work zone's floor unless you change it.
- Drag from a face to draw a rectangle on its plane, its corners rounded outwards to the grid.
- Double-click a face to add its corners.
- Hold <kbd>Shift</kbd> over a flat outline, which fills to show it can be picked, and drag to extrude it along its
  normal in grid steps.

In a 2D view:

- Click to add a grid point at the depth of the last point, or at the work zone's floor.
- Drag over empty space for a rectangle at that depth. Drag a point to move it, and any points drawn on top of it,
  across the view.
- <kbd>Shift</kbd>-drag a flat outline in a view that shows its side to extrude it: draw a rectangle in **Top**, then
  extrude it upwards in **Front**.

The points are checked in the background as you add them. The readout says how many corners and faces the brush will
have, or why there is no brush yet: fewer than four points, or points on one line or one plane. A dashed line shows
the outline while it is flat and a solid one the checked brush. Press <kbd>Enter</kbd> in any view, or choose **Create
Assembled Brush**, to add it in the material chosen on the **Textures** tab. As with **Create Brush from Points…**, it
goes into the open group or the active layer, is selected and is one undo step, and a locked layer stops it with a
message. The points then clear and the tool stays on.

<kbd>Backspace</kbd> or <kbd>Delete</kbd> removes the last point. <kbd>Esc</kbd> cancels a drag, then clears the points,
then leaves the tool; choosing another tool, such as the clip tool or a vertex mode, also leaves it. Hidden brushes
cannot be clicked. Up to 4,096 points within ±32768 units are kept. `map add-brush --points` with `--extrude dx,dy,dz`
builds the same brushes from the [command line](manual/cli.md). To type or paste exact points, use
[Create Brush from Points…](manual/levels.md#create-a-brush-from-typed-points).

#### Copy vertex coordinates

**Partial:** TrenchBroom's debug **Print Vertices** as an ordinary command. Choose **Copy Vertex Coordinates**
(**Edit** > **Brush Shapes**, the **Geometry** menu above the views, the **Vertices** section of the **Tools** tab, or
command search). With faces, edges or vertices picked in a component mode, it copies their corners; otherwise it copies
every corner of each selected brush, including the brushes of a selected brush entity. Hidden and filtered brushes are
left out, and the map does not change.

Each brush or component gets a comment line such as `// brush:3` or `// brush:3 face:2 base/metal`, followed by its
corners as `(x y z)` at full precision: the shortest decimal that reads back as exactly the stored value. The same text
goes to the **Activity** panel. Paste it into **Create Brush from Points…**, a script or a report.
`map brush-components` lists one or more brushes, with each face's corner coordinates, from the
[command line](manual/cli.md).

#### Work with layers

**Partial:** layers work on Quake, Quake II, Quake III Arena and Doom maps. Create and name them in the **Scene**
section of the **Outliner** tab; these commands use them straight from the views.

| To | Do this |
| --- | --- |
| Select whole layers | Select an object in each layer and choose **Expand Selection to Layers**, as in NetRadiant Custom. Every visible, unlocked object in those layers joins the selection, nested groups included. Closed groups stay whole, and a group with a hidden, locked or filtered member is left out; the status bar says how many. An open group keeps the result inside it. A brush entity joins through its brushes, and worldspawn belongs to no layer. |
| Move objects to a layer | Choose **Move to Layer…** for a menu of the layers at the pointer, or the **Move to Layer** submenu of the right-click menu, as in TrenchBroom, and pick a layer. The selection moves as one undo step. A member of a closed group moves its whole group, and a brush of a brush entity moves the entity with its brushes. Objects moved into a hidden layer leave the selection. The layer the selection is in already, a locked layer, or locked objects grey the entry out with the reason. Close an open group first: layers hold whole groups. |

In the **NetRadiant Custom** profile, <kbd>Shift</kbd>+<kbd>Q</kbd> opens the layers menu and
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Q</kbd> expands the selection: NetRadiant Custom's <kbd>Ctrl</kbd>+<kbd>Q</kbd> stays
**Quit** here. Other profiles leave both without keys; add your own in **Help** > **Keyboard Shortcuts**. Both commands
are in the **Select** menu above the views, on the **Tools** tab and in command search.

Expanding the selection adds an undo step in the TrenchBroom profile only, as **Select Siblings** does. Doom lines and
vertices belong to a layer only when you assign them, so expanding from a sector does not add its unassigned lines.
The command line does the same with `editor scene select --expand-layers` and `editor scene move-to-layer`; see
[Expand and move map layers](manual/cli.md#expand-and-move-map-layers).

#### Edit a group

In **Scene**, create a named group and choose **Assign selection**. Clicking any
member then selects the whole closed group, including nested and hidden members.
Transforms move the assembly together. Layers remain separate organisation
containers; they do not select as units.

Choose **Open Group** from **Select**, **Tools > Groups** or command search, or
double-click the group in a viewport. Its contents become editable and other
objects stay visible but cannot be picked or changed. Nested groups remain
closed until opened. The Scene breadcrumb and viewport label show where you are.
Open the containing group before editing individual vertices, edges or faces.

New objects and prefab placements join the open group. **Create in** stays fixed
while it is open. **Close Group** returns to its parent and selects the group;
**Close All Groups** returns to the map and restores the previous creation
destination. Scene has named Open/Close buttons. Double-clicking empty or outside
space also closes the current group when no editing gesture owns that action.

Opening and closing do not dirty the map or add undo steps. Closing cancels
pending component and clip drafts. Locks still apply, and an edit that would
change shared geometry outside the group is refused as a whole. Builds and
packages include the complete map. Group context is not saved between sessions.

**Duplicate**, offset arrays, copy/paste and prefab capture preserve complete
closed groups, nested groups and empty descendants. New groups receive fresh
identities and distinct sibling names. Duplicates stay beside their source;
pasted assemblies and prefabs join **Create in** or the open group. Opening a
group before copying its contents leaves that open ancestor out of the copy.
Partial object copies retain the usual object-copy behaviour. Each insertion,
including its hierarchy, is one undo step.

**Duplicate** on a linked group creates another member of its link set. Copying
one member to the clipboard or a prefab makes an independent group. Copying
two or more members of a set preserves their link with a fresh identity for
each insertion. Groups nested in a linked group are copied with it. Placing a
prefab that contains linked groups turns and moves their frames with it, at any
angle. Group visibility and locks are retained. Clipboard scene metadata that is stale or malformed is
refused before insertion.

Grouped prefabs use `.vprefab` version 2; flat version 1 assets remain supported.
Doom and UDMF thing duplication and arrays also preserve complete groups.
Doom geometry duplication, standalone empty-group copying and automatic removal
of emptied groups remain incomplete.

#### Repeat parts with linked groups

**Status: Partial.** Linked groups keep repeated parts of a level the same, as TrenchBroom's do: a row of pillars,
a window frame, a lamp with its light. Groups that share their content are linked groups, and together they form a
link set. None of them is the original: an edit to the content of any one is made to every other, each in its own
place. (Blender's **Duplicate Linked** is the same idea for meshes; Hammer's instances differ, as they draw another
map file.)

1. In the **Scene** section of the **Outliner** tab, type a name and choose **New group**, then select the objects
   and choose **Assign selection**.
2. Select the group (with it closed, clicking any of its objects selects it) and choose **Create Linked Duplicate**
   (the map views' right-click **Groups** menu, **Tools** tab, **Groups**, or command search;
   <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>D</kbd> in the TrenchBroom profile). The duplicate is a new group beside the first, named after it ("Pillar 2"), linked with it and
   selected. **Linked** follows both names in the **Scene** tree.
3. Move, rotate, mirror or scale the duplicate as a whole, at any angle. Only that group changes; it remembers how
   as its frame.
4. Open any of the groups and change it: edit a brush, a key or a texture's alignment, or add, delete or regroup
   objects. Every other group in the set takes the change at once, turned, mirrored or scaled into its own frame,
   and one **Undo** puts them all back. Inside an open group every change is shared, even one that moves all of its
   content.

Groups can sit inside linked groups. A linked duplicate copies them too and links each nested group with its copy,
so an edit inside a nested group reaches every copy of it; linked groups can hold linked groups. A group cannot hold
a group it is linked with.

While the selection holds a linked group, or one is open, the 2D views and the 3D camera draw a box around every group
of its set with its name, and arrows from the selected group to the others. The selected or open group's box is solid
and the rest of its set dashed. In the camera, boxes and arrows are drawn over the scene with a dark edge, and a name
shows where it fits beside its box, clear of the view's readouts and the selection's resize handles.
**Show > Linked Groups** draws every linked group, fainter, in both.

| Command | What it does |
| --- | --- |
| **Create Linked Duplicate** | Duplicates the selected group beside itself and links the two. **Duplicate** on a linked group adds another member to its set. |
| **Select Linked Groups** | Selects every group linked with the selected ones. |
| **Update Linked Groups** | Makes the rest of the set match the selected group, such as groups out of sync. |
| **Separate Linked Groups** | Takes the selected groups out of their set, keeping their content. Several chosen from one set stay linked with each other; a group left on its own is no longer linked. |
| **Extract Linked Groups** | Moves part of an open linked group, and its counterparts, into a new set. |
| **Add Protected Property…**, **Clear Protected Properties** | Keep entity keys local to one group, or share them again. |

A group out of sync no longer matches the rest of its set. That happens when one edit changes two members of a set
in different ways (VibeStudio keeps both edits, where TrenchBroom refuses the edit; values a member protects do
not count), or when an update passes a locked group by. The status bar and **Activity** say so; the **Scene** tree shows **Linked, out of sync**, the 2D
views and the 3D camera draw the group dash-dotted in the warning colour, and saving lists it. Edit one member again, or select the one
to keep and choose **Update Linked Groups**. The **Scene** section's **Linked duplicate**, **Separate**, **Update
linked** and **Select linked** buttons act on the group chosen in its tree.

Linked groups work on Quake-family maps. Links and frames are saved in the map's VibeStudio scene note, a comment at
the end of the file that other editors skip, so they see ordinary brushes and entities; maps saved by earlier
versions, whose frames were quarter turns and a mirror, still open. To keep groups and linked groups in a map you
also edit in TrenchBroom, see [Share layers and groups with TrenchBroom](manual/levels.md#share-layers-and-groups-with-trenchbroom).

Corresponding objects retain their identities through edits, **Undo**,
save/reopen and transfers of whole sets through copy/paste and prefabs. Creating or updating linked groups
remains one complete undo step even with a one-step history limit. If a
linked operation cannot fit the scene limits, it leaves the map unchanged.
For older linked maps, use **Update Linked Groups** once to establish
correspondence before using extraction or protected properties.

#### Share layers and groups with TrenchBroom

**Status: Partial.** TrenchBroom keeps its layers, groups and linked groups in the map itself, as `func_group`
entities with keys of its own (`_tb_type`, `_tb_id`, `_tb_name`, `_tb_linked_group_id` and `_tb_transformation`), and
an entity lists its protected keys in `_tb_protected_properties`. VibeStudio reads and writes the same keys, so a map
can go back and forth between the two editors with its groups intact.

- **Open** a map TrenchBroom saved: its layers, groups and linked groups appear in the **Scene** tree, each group in its
  layer, the linked groups with their frames and protected keys, so an edit to one reaches the others straight away.
  The `func_group` entities themselves are not listed as entities; their brushes become world brushes in the layer or
  group. A group named the same as a sibling gets a number ("pillar 2"), and TrenchBroom's name is written back.
  Linked groups that no longer match each other open out of sync, and the **Health** tab says which.
- **Save** a map that opened that way and it keeps TrenchBroom's form, so TrenchBroom opens it with the same layers,
  groups and linked groups. To give any map that form, choose **Level map with TrenchBroom groups (*.map)** as the file
  type in **Save As**; later saves keep it, and **Level map (*.map)** goes back to VibeStudio's note alone. A layer's
  colour and **Omit from export**, which VibeStudio does not show, are kept and written back.
- From the CLI, `editor scene save <map> --scene-format trenchbroom --output <file>` writes a map in TrenchBroom's
  form, and `--scene-format vibestudio` takes it out again; any other `editor scene` change accepts the same option.
  See [Command line](manual/cli.md).

A map saved with TrenchBroom groups still carries VibeStudio's scene note, after TrenchBroom's keys. While the note
matches the map, VibeStudio reads the note, because it also holds what TrenchBroom does not keep: hidden and locked
groups, which linked groups are out of sync and which objects correspond. TrenchBroom drops the note when it saves,
and VibeStudio then reads TrenchBroom's keys; a note that no longer matches the map is set aside, and the **Health**
tab says so. A map whose TrenchBroom entities share lines with other text opens with them as ordinary `func_group` entities, as
earlier versions opened it, and the **Health** tab says why.

#### Extract part of a set of linked groups

**Status: Partial.** Open a linked group, select some of its contents, then choose
**Extract Linked Groups** in **Tools > Groups** or command search. The selected
contents become a new set of linked groups across every member. Each original
group keeps its remaining contents. Later edits are shared within each set
independently.

Selecting a brush entity extracts all its geometry. Selecting only some of its
brushes or patches splits the owner, retaining that instance's entity properties
and protection on both halves. Each copy keeps its position, supported turn,
mirror, visibility and parent. Geometry and texture coordinates stay in place.
The operation closes the source group, selects its new counterpart, and keeps
the parent editing context open. One **Undo** restores both families, the original
selection and the open group.

A closed group inside the open one is selected whole. In every member it moves
into the new group with everything in it, keeping its link, frame, name and
visibility, so it stays linked with its counterparts. To take part of a nested
group, open that group instead. When the open group is itself inside a linked
group, every group linked with that one takes the same new structure and stays
in sync.

Every instance must be unlocked, contain the selected corresponding objects and
have some contents left behind; nested groups that move must be unlocked too.
Linked groups around the open group must each hold the same counterparts.
Assign each complete brush entity to its group; resolve geometry explicitly
assigned outside that group before extraction. Legacy or incomplete
correspondence requires **Update Linked Groups** first.
Cancellation, stale selection, scene limits and invalid ownership leave the map
unchanged. Preparation runs in the background with cancellable progress.

The shared CLI operation is:

```sh
vibestudio --cli editor scene extract-linked-groups assembly.map --context <group-UUID> --objects brush:6,entity:4 --output extracted.map --json
```

Use `editor scene list` on the current file to inspect node UUIDs and native
selectors; native object numbers can change when saving. To move a nested group,
list all of its objects in `--objects`. The extraction report returns durable
UUIDs for the new groups and counts the nested groups moved (`nestedGroups`).
Add `--dry-run` to validate without writing, or `--overwrite` to replace an
existing output. Extracted sets use the normal copy/paste and prefab workflows
from their containing editing context.
Native cross-platform acceptance remains an open gate.

#### Keep entity properties local to one linked group

**Status: Partial.** Open a group and select its entities. The entity inspector's
**Protected** column shows **Local**, **Shared** or **Mixed** with a check box.
Use <kbd>Tab</kbd>/<kbd>Shift</kbd>+<kbd>Tab</kbd> to navigate property cells.
Toggle the check box, or focus its cell and press <kbd>Space</kbd> or <kbd>Enter</kbd>, to
change protection for the selected entities. **Local** preserves that instance's
value when a corresponding linked entity changes. A protected key without a
value remains absent. These keys appear under **Protected keys without values**.

Use **Add Protected Property…** in **Tools > Groups** or command search to set a
key and value together. The corresponding entities of the other linked groups keep
their values or absence. Ordinary value edits still work in the inspector. Property keys follow
the editor's case-insensitive matching. Worldspawn properties cannot be protected.

Unchecking a key restores an existing value from an unprotected corresponding
entity, when available. Otherwise it retains the current value or absence.
**Clear Protected Properties** applies this rule to every protected key on the
selected linked entities. Each operation is one undo step. Clearing ignores
unlinked entities; their individual check boxes remain available. Locks and the
open group's editing boundary apply. An open property dialog refuses to commit
if its map, selection or project has changed.

Protection belongs to each group. A linked update preserves the destination's
value whenever either side protects that key, and retains the destination's own
protection flags. Save/reopen, duplication, clipboard and prefab transfers retain
the flags; separating a linked group retains them too. For older linked maps, use
**Update Linked Groups** before adding protection so correspondence is known.
The scene's authoring metadata stays in the native map's editor trailer; the
compiler and package workflows receive the actual entity values and asset paths.

The same services are available through the CLI:

```text
vibestudio --cli editor scene set-protected-property arena.map --objects entity:2 --key targetname --value local_relay --output local.map
vibestudio --cli editor scene protect-properties local.map --objects entity:2 --keys origin,angle --protected true --output protected.map
vibestudio --cli editor scene clear-protected-properties protected.map --objects entity:2 --output shared.map
```

Use `--protected false` to unprotect selected keys, `--value=` for an explicitly
empty value, `--context <group UUID>` to constrain the operation to an open group,
and `--dry-run` to validate before writing. Replacing a file requires `--overwrite`.
`editor scene list --json` reports each entity's protected keys. Native platform
keyboard/screen-reader acceptance and full TB/NRC parity remain unverified.

### Edit Doom-family maps

| To | Do this |
| --- | --- |
| Add things | Choose **Add Thing…** and give a DoomEd number, or drag a thing type from the **Things** tab onto the **Top** view. |
| Draw a sector | Turn on **Draw Sector** (<kbd>D</kbd>), click the corners, then click the first corner again or press <kbd>Enter</kbd>. <kbd>Backspace</kbd> removes the last corner and <kbd>Esc</kbd> drops the shape. **Add Sector…** takes corners typed as `x,y` pairs. New lines join existing vertices and split the lines they land on. |
| Make sectors from lines | Turn on **Make Sector Mode** (**Tools** tab, **Sectors and Lines**) and click inside any closed shape of lines in the **Top** view, as in Doom Builder's Make Sectors mode: the area becomes a new sector, islands of lines inside it included, and lines that faced nothing there open onto it. Clicking inside an existing sector makes it anew. <kbd>Esc</kbd> turns the mode off. |
| Split or flip lines | **Split Linedefs** splits each selected linedef at its middle; **Flip Linedefs** turns it round, sides and all. |
| Curve lines | **Curve Linedefs…** bends each selected linedef into the number of **Segments** you choose, along an arc whose middle **Bulge**s towards the line's front side (a negative bulge, its back), as Doom Builder's curve mode does. Texture offsets carry on along the pieces. |
| Merge vertices | Select vertices, picking the one to keep last, and choose **Merge Vertices**. |
| Join or merge sectors | **Join Sectors** makes the selected sectors one sector, the one picked last. **Merge Sectors** also removes the lines between them, except lines with a special or tag. |
| Make doors | **Make Door…** turns the selected sectors into doors, as Doom Builder does, with the door, track and ceiling textures you choose. |
| Raise and lower | In a 2D view, <kbd>Page Up</kbd> and <kbd>Page Down</kbd> move floors by 8 units, <kbd>Ctrl</kbd> moves ceilings instead, and <kbd>Shift</kbd> makes the step 1. **Brighten Sectors** and **Darken Sectors** change light by 16. In the camera, as in Doom Builder's visual mode, **Raise Surface at Crosshair** and **Lower Surface at Crosshair** move the floor or ceiling you aim at by 8, and **Brighten Sector at Crosshair** and **Darken Sector at Crosshair** change its light, and **Nudge Texture Left**, **Right**, **Up** and **Down at Crosshair** slide the texture of the wall you aim at by one unit. With **Drag Textures in Camera** on, drag a wall's texture with the left button and it follows the pointer, the camera showing it as you go; letting go makes one undo step, and <kbd>Esc</kbd> puts it back. They are in the **Geometry** menu, the **Tools** tab and command search, and you can give them keys in **Help** > **Keyboard Shortcuts**. |
| Align textures | **Align Textures at Crosshair** lines up the walls joined to the one you aim at in the camera that show the same texture, as Doom Builder's auto-align does: the texture runs on across each join and its rows stay level where floors and ceilings change. In a 2D view, **Align Wall Textures** starts from the first selected linedef and keeps to the selected linedefs when you select several. One undo step. |
| Grade sectors | Select three or more sectors in order and choose **Gradient Floors**, **Gradient Ceilings** or **Gradient Brightness**. |
| Edit fields | The **Inspector** edits a thing's type, angle, position and flags, a sector's heights, flats, light, special and tag, a linedef's special, tag or Hexen arguments, flags and both sides' textures and offsets, and a vertex's position. |
| Delete | <kbd>Delete</kbd> removes the selected things, vertices, linedefs or sectors. |

Edits that move or add lines and vertices leave the map's nodes out of date: rebuild them on the **Build** page before
you play. Raising, lowering and grading keep the nodes current.

For UDMF maps, choose **Edit** > **UDMF Properties…** to change any property of the map or of one object, keeping
comments and unknown fields. Moving, rotating, mirroring, snapping and resizing work too. **Duplicate Selection** and
**Duplicate with Offset…** copy existing things, including their optional height. Copies keep game IDs and action
arguments, so review these in **UDMF Properties…** when a copy needs independent behaviour. Ordinary thing copies keep
nodes current; copying polyobject controls requires a node rebuild. Creating a new thing from a type, deleting things
and creating or copying geometry are not available for UDMF yet.

### Undo, redo and recover

- **Undo** (<kbd>Ctrl</kbd>+<kbd>Z</kbd>) and **Redo** (<kbd>Ctrl</kbd>+<kbd>Y</kbd> or
  <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd>) step through map edits; their buttons sit at the start of the view
  toolbar. The **History** tab lists every edit; press <kbd>Enter</kbd> on a step to go back or forward to it.
- Map undo is separate from package undo on the **Packages** page.
- In the **TrenchBroom** profile, object picks, **Select All**, **Select None**,
  **Invert Selection**, selection from the object/scene lists and opening or
  closing groups also enter this history. Undo restores selection order and the
  group being edited. These steps do not mark the map modified; every history
  position with the saved contents is labelled **saved**. Choosing a different
  selection after Undo starts a new branch and discards Redo. Repeating the same
  selection keeps Redo. NRC and the default profile keep selection and group
  navigation outside history. Face/component picks, visibility and filters do
  not yet have complete TB selection-history behaviour.
- VibeStudio saves a recovery checkpoint of a changed map every minute. **File** > **Recover Maps…** lists the
  checkpoints; **Restore** opens one as unsaved work and never replaces your map file. The window's check box turns
  checkpoints on or off. Checkpoints stay on your computer.

### Copy and paste between editors

In a Quake-family map, <kbd>Ctrl</kbd>+<kbd>C</kbd> copies the selected entities, brushes and patches as plain `.map`
text, the same entity and brush blocks that TrenchBroom and the Radiant editors exchange. <kbd>Ctrl</kbd>+<kbd>V</kbd>
adds `.map` text from the clipboard at its own coordinates and selects it, whichever editor it came from, and
<kbd>Ctrl</kbd>+<kbd>X</kbd> cuts. **Paste with Offset…** previews the pasted objects at an offset you type. Pasted
brushes keep the face format they were copied in, so paste between maps that use the same format. Doom maps have no
clipboard; use **Duplicate Selection** for things.

### Save your work

1. Choose **Save** in the header, or **File** > **Save Map** (<kbd>Ctrl</kbd>+<kbd>S</kbd> while the page has focus).
   VibeStudio writes the map back to its own file, after copying the old file into `.vibestudio/map-backups` beside it.
2. A new map, or one opened from a package, asks for a location first.
3. To write a separate file instead, choose **File** > **Save Map As…** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>S</kbd>).
   VibeStudio suggests the name with `-edited` added and keeps editing the new file.

> [!IMPORTANT]
> **Save** replaces the file you opened, keeping a backup of the old content. Use **Save Map As…** to leave the
> original untouched, for example for a map inside a game installation.

Saving runs in the background with a **Cancel** button; a cancelled or failed save leaves the destination unchanged.
If the file changed outside VibeStudio after you opened it, **Save** refuses: use **Save Map As…**, or **Reload** to
read the outside version. A map is always saved in the format it was opened in. A WAD is written whole, with its other
maps and lumps kept. VibeStudio does not convert between Doom and Quake formats or between face formats, apart from the
optional Valve 220 conversion.

In Quake-family maps, a brush you reshape keeps its place in the file. Component moves, the brush editor, split
extrusion, clipping, CSG and wrap/merge write the changed brush back where the original stood, so the brushes after it
keep their order and the "entity N, brush M" numbers a compiler reports or **Find Brush by Number…** uses. When an edit
makes several pieces, the first piece takes the original's place and the others join the end of the same entity, as
NetRadiant Custom adds a split fragment. New brushes, copies and pastes also go at the end of their entity, so a brush
added to worldspawn still shifts the numbers of brushes in later entities.

### Check maps and entities

- **Health tab.** Lists map problems, entity problems, leaks and compiler checks. With a package open, it also checks
  every texture the map uses. Press <kbd>Enter</kbd> on an entity problem to select that entity.
- **Map tab.** **Worldspawn** edits the map's own keys; **Add Key…** suggests the ones its definition knows.
  **Checklist** shows what a build needs and what is missing, such as a player start, lights, a title, a `wad` key
  for Quake or deathmatch starts for Doom; press <kbd>Enter</kbd> on a row to go to what it is about.
  **View** > **Level Sidebars** > **Show Map Statistics** and **Show Worldspawn Keys** open the tab with that section
  expanded and the keyboard in its list; both are in command search and can be given keys.
- **Entity definitions.** On the **Map** tab, give a Radiant `.def`, Valve `.fgd` or Quake III `.ent` file, or a
  folder of them, under **Entity Definitions** and choose **Load** (**Browse** picks a file). Left empty, VibeStudio
  looks in the project's `.vibestudio/definitions`, `definitions`, `defs`, `scripts`, `base/scripts` and `entities`
  folders. **Project** > **Load Entity Definitions…** does the same. The map is then checked for unknown classes,
  undeclared keys, invalid values, missing required keys, unknown spawnflags, and targets that name nothing. An `.ent`
  file lists placed entities only, so it cannot check key types or spawnflag names.
- **Built-in classes.** With no definitions found, a Quake-family map is checked against the classes of the stock
  game it looks like: Quake, Quake II or Quake III Arena, taken from id Software's GPL game code, with the
  Quake compiler classes VibeMap2 inherits from ericw-tools, such as `func_detail`. A mod's own classes read as unknown until you load its
  definitions, and **Health** and the **Checklist** say which classes were used.
- **Dependencies.** Choose **Dependencies** in the header. The scan reads the package or asset folder open on the
  **Packages** page or, with none open, the project's folders. It lists textures, shader images, models, model
  materials and sounds, marking missing and ambiguous ones; with the game's assets indexed (see
  [Package and release](manual/releases.md#before-you-start)), references the game provides show as **Provided by the
  game**. Tick **Problems only** to filter, **Select in Map** to find the objects that use an asset, and **Copy
  JSON** for the full report. **Export Assets…** writes the resolved files of a package to a new package; when the
  scan read the project, **Package Map…** releases the map instead.
- **Package Map.** Choose **Package Map** in the header to release the map with its build and every custom asset it
  uses, leaving the game's own files out. See [Package and release](manual/releases.md).
- **Leaks.** **Build** > **Load Leak Trail…** draws a compiler `.pts` or `.lin` file over the map.
- **Portals.** **Build** > **Load Portal File…** outlines the vis portals of a compiler `.prt` file (PRT1, PRT1-AM or
  PRT2) in the 2D views and the camera, to see where visibility is cut and where detail or hint brushes would help.
  **Clear Portals** removes them.

### Build and test the map

- On the document bar, pick a compiler profile and choose **Run Profile** to review and run a compile of the open map.
  The compiler reads the saved file, so VibeStudio asks you to save first. **Copy CLI** copies the matching command.
- On the **Build** page, **Use Open Map** builds the map open in Levels and follows it when you open another.
- For Quake, Quake II and Quake III maps, **Build** > **Prepare Build Workspace…** captures the map, unsaved edits
  included, with the open package into a new build folder. **Use in Build** makes it the build input; **Publish
  Prepared Build…** and **Deploy Prepared Build…** package and install the result.

[Build and launch](manual/build-and-launch.md) covers compilers, pipelines, node builders and launching the game.

### Generate a level (optional)

**Generate** in the header plans and builds a sealed level from a short description for Quake, Quake II, Quake III or
Doom. Under **Who plans the rooms**, **The rules, on this machine** needs no AI; **The text model** uses your configured
model, which it names once it is ready, and needs a description to plan from; with it chosen, **AI Settings…** beside
**Preview Request…** opens the AI settings. You review the layout, then open it in Levels as a new, unsaved map or
save it.

**Generator** chooses who builds it:

| Generator | Games and modes | What you get |
| --- | --- | --- |
| **Studio blockout** (Available) | Quake, Quake II, Quake III, Doom; single player, deathmatch, duel | Rooms on a grid of sealed cells, themed textures, liquid pits, monsters |
| **Quake-MapGen** (Partial) | Quake, Quake II, Quake III; single player, deathmatch, duel, capture the flag | Ranked layouts with stairs and raised crossings, a choice of **Map format**, and an **Analysis** of navigation, visibility and lighting |

Quake-MapGen reads the theme from the description, so **Theme**, **Liquid**, **Monsters** and
**Use the project's textures where they fit** are hidden while it is chosen; **Intensity** and **Map format** appear
instead, and **Rooms** goes up to 32 rather than 16. Its findings are advisory: read them under **Details** >
**Analysis**, and keep its full report with **Save Report…**. **Edit with AI** asks your text
model for changes to an open Quake-family map and lists each proposed edit for review before anything changes. AI is
off by default; see [AI assistant](manual/ai.md).

### Work from the keyboard

Every command is also in the menus and in command search. Your [editor profile](manual/editor-profiles.md) can change these
keys, and your own keys in **Help** > **Keyboard Shortcuts** take priority over both. Every Levels command is also
listed, with a short description, in the shared command registry that `vibestudio --cli ui semantics --json` prints,
so scripts and editor profiles name each one by the same id, such as `map.vertex-tool` for **Edit Vertices**.

<details>
<summary>All shortcuts on the Levels page (VibeStudio Default profile)</summary>

| Action | Shortcut |
| --- | --- |
| Open Map / New Map | <kbd>Ctrl</kbd>+<kbd>M</kbd> / <kbd>Ctrl</kbd>+<kbd>N</kbd> |
| Save Map / Save Map As | <kbd>Ctrl</kbd>+<kbd>S</kbd> / <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>S</kbd> |
| Undo / Redo | <kbd>Ctrl</kbd>+<kbd>Z</kbd> / <kbd>Ctrl</kbd>+<kbd>Y</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd> |
| Copy / Cut / Paste | <kbd>Ctrl</kbd>+<kbd>C</kbd> / <kbd>Ctrl</kbd>+<kbd>X</kbd> / <kbd>Ctrl</kbd>+<kbd>V</kbd> |
| Duplicate / Delete | <kbd>Ctrl</kbd>+<kbd>D</kbd> / <kbd>Delete</kbd> |
| Select All / Select None / Invert Selection | <kbd>Ctrl</kbd>+<kbd>A</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>A</kbd> / <kbd>Ctrl</kbd>+<kbd>I</kbd> |
| Hide Selection / Show All Hidden | <kbd>H</kbd> / <kbd>Shift</kbd>+<kbd>H</kbd> |
| Smaller Grid / Larger Grid | <kbd>[</kbd> / <kbd>]</kbd> |
| Clip Tool / Draw Sector / 3D Wireframe | <kbd>X</kbd> / <kbd>D</kbd> / <kbd>W</kbd> |
| Raise / Lower Floor (2D view) | <kbd>Page Up</kbd> / <kbd>Page Down</kbd>; add <kbd>Ctrl</kbd> for ceilings, <kbd>Shift</kbd> for steps of 1 |
| Maximize or Restore Active View (this profile only) | <kbd>Ctrl</kbd>+<kbd>Space</kbd> |
| Frame the selection / the whole map (2D view) | <kbd>F</kbd> / <kbd>Home</kbd> |
| Zoom in / out (2D view) | <kbd>+</kbd> / <kbd>-</kbd> |
| Select the next object / add the next or previous (2D view) | <kbd>Tab</kbd> / <kbd>Ctrl</kbd>+<kbd>Tab</kbd>, <kbd>Shift</kbd>+<kbd>Tab</kbd> |
| Nudge the selection | The profile's **Nudge** keys, one grid step (VibeStudio Default: arrow keys in a 2D view); <kbd>Shift</kbd> moves eight steps where that key is free; see [Nudge the selection](manual/levels.md#nudge-the-selection) |
| Pan (2D view) | Arrow keys with nothing selected, or <kbd>Ctrl</kbd>+arrow keys; <kbd>Space</kbd> turns a pan hand on or off |
| Cancel a drag, clear the selection, then leave the view | <kbd>Esc</kbd> |
| Map actions menu | <kbd>Menu</kbd> or <kbd>Shift</kbd>+<kbd>F10</kbd> |

</details>

### Use the command line

The `map` commands use the same services as the page. Commands that change a map write a new file named by
`--output`, preview with `--dry-run`, and need `--overwrite` to replace an existing file. For a WAD, name the map with
`--map-name` (or `--map`). Objects are named like `entity:3`, `brush:12`, `patch:0`, `thing:5`, `vertex:12`,
`linedef:12` and `sector:4`.

```sh
vibestudio --cli map inspect ./maps/start.map --json
vibestudio --cli map move ./maps/start.map --object brush:12 --delta 16,0,0 --output ./maps/start-moved.map --dry-run
```

<details>
<summary>Main map commands</summary>

| Command | What it does |
| --- | --- |
| `map inspect` | Show a map's entities, textures, statistics and validation |
| `map find` | List the objects a query matches, as the **Objects** filter does; `--number 0,12` finds the object a compiler names as `Entity 0, Brush 12` |
| `map new` | Create an empty map or starter room |
| `map edit` | Change entity keys, a face's texture and alignment, or Doom sector, linedef and side fields |
| `map add-entity`, `map add-brush`, `map add-thing` | Add a point entity, a brush shape, the brush enclosing typed points (`--points`) or a Doom thing |
| `map delete`, `map duplicate`, `map paste` | Delete objects, copy them with an offset (`map duplicate --copies N` creates a linear array), or insert `.map` text |
| `map move`, `map rotate`, `map flip`, `map resize`, `map snap` | Transform objects, with texture lock options |
| `map round-to-grid` | Round each brush vertex, patch control point and point-entity origin of `--object` objects onto `--grid`, or only `--set` components, as **Round Selection to Grid** does; optional `--uv-lock off\|on\|fit` |
| `map repeat` | Replay an exported JSON recipe on the current object targets as one checked edit |
| `map add-shape` | Add a shape of the **Shapes** tab, or replace brushes with one |
| `map clip`, `map hollow`, `map carve`, `map intersect`, `map merge-brushes`, `map wrap-merge`, `map snap-vertices` | Brush tools; **Wrap/Merge** fills a convex hull from objects or components; `map hollow --mode touch` or `room` hollows as **Hollow Touching…** or **Make Room** does, with `--caulk`, `--caulk-texture` and `--open-face` for its caulk and openings |
| `map brush-components`, `map move-components`, `map insert-vertices`, `map remove-components`, `map split-components` | Inspect current component IDs, move components, insert corners with repeated `--position x,y,z`, remove incident vertices, or split edges/faces with `--kind edge\|face`, repeated `--component` IDs and `--delta x,y,z`; edits require `--brush` and `--output` |
| `map transform-components` | Move, rotate, scale or shear brush components and patch control points in one batch, or `snap` one geometric brush vertex to `--target brush:ID/vertex/ID`; repeat `--set object:id/kind/component,component`, choose `--operation`, and supply `--output`; shared validation, undo policy and exact UV lock |
| `map select-components` | Read-only conversion, Grow, Shrink or Connected queries with repeated `--set`; conversion requires `--to` and accepts complete/touching coverage; Grow/Shrink accept `--steps 1..256`; JSON selectors feed component transforms |
| `map extrude-faces` | Move parallel brush planes with repeated `--face brush:face`, signed `--distance` and `--output`; optional `--split`, `--grid` and `--no-texture-lock` |
| `map sweep` | Add straight, arc or S-bend sections from repeated `--set brush:ID/face/IDs`; set destination transform, segments, iterations, material and alignment; retain source objects |
| `map create-entity` | Create an entity of a class of the definitions, as the **Create** commands do: a point class at `--origin x,y,z`, or placed against the map from `--camera`, `--plane-point` or `--ray-origin`; a brush class from the `--object` brushes |
| `map tie-entity`, `map move-to-world`, `map detail` | Make brush entities, give their brushes back to the world (`--selected-only` for just the given ones), and make brushes detail or structural (`--structural --policy trenchbroom` also takes them out of brush entities) |
| `map move-to-entity`, `map explode-entity` | Move brushes and patches into an existing entity (`--target last`, `first` or `entity:N`), and give each brush of an entity its own copy of its keys (`--rename-targetnames` for unique names) |
| `map select-region`, `map drop-to-floor` | List the objects a region selection picks, and drop entities to the floor |
| `map align`, `map shear`, `map region` | Line objects up, slant them, and write a region as a sealed map of its own |
| `map replace-key` | Find and replace a key's values across the entities |
| `map entity-colour` | Set the `_color` of entities and of the entities holding brushes (`--colour`), or normalise it (`--normalise`, `--each` for each its own) |
| `map apply-texture`, `map replace-texture`, `map align-textures` | Texture tools; `map replace-texture --face` replaces only picked faces |
| `map add-patch`, `map edit-patch`, `map stitch-patches`, `map cap-patch`, `map patch-matrix`, `map thicken-patch` | Quake III patch tools |
| `map export-prefab`, `map inspect-prefab`, `map insert-prefab` | Save objects as a prefab, inspect one, and place one at `--position` or in place of objects with `--replace` |
| `map connect` | Connect entity owners to the last selected destination; `--profile netradiant-custom` selects NRC naming and two-object rules, `--kill-target` selects KillConnect, and `--game` supplies game context |
| `map draw-sector`, `map draw-stairs`, `map draw-grid`, `map split-linedef`, `map curve-linedefs`, `map flip-linedef`, `map merge-vertices` | Doom geometry tools |
| `map join-sectors`, `map merge-sectors`, `map make-door` | Doom sector tools |
| `map align-walls` | Line up Doom wall textures along the walls joined to one side |
| `map make-sector` | Make a Doom sector of the lines around a point |
| `map shift-sectors`, `map gradient-sectors` | Raise, lower or grade Doom sectors |
| `map inspect-udmf`, `map edit-udmf` | Read and edit UDMF properties |
| `map textures`, `map dependencies` | Check a map's textures and assets against a package or folder; `--installation <id>` marks what the game provides |
| `release plan`, `release publish` | Package and release a map with `--map <map>`; see [Package and release](manual/releases.md) |
| `map render` | Draw an SVG picture of a map |
| `map compile-plan` | Plan a compile with a compiler profile |
| `map recoveries`, `map recover` | List recovery checkpoints and save one to a file |
| `map generate` | Generate a level from a description; `--generator quake-mapgen` builds it with Quake-MapGen, and `--planner ai` asks your text model for the plan |
| `entity definitions`, `entity validate` | List entity classes, and check a map's entities against them |
| `editor scene create-linked-duplicate`, `editor scene select-linked-groups`, `editor scene update-linked-groups`, `editor scene separate-linked-groups` | Duplicate a scene group and link the two, select its link set, make the rest of its set match it, or take groups out of their set (`--id a,b` keeps several linked with each other). `link`, `update-links` and `unlink` still work |
| `editor scene select` | Resolve group selection without writing; use `--objects` and optional `--context <group UUID>`, `--selection` and `--mode replace\|add\|toggle` |
| `editor scene translate` | Move closed groups or objects inside `--context` with `--objects`, `--offset x,y,z` and `--output`; supports `--dry-run` and `--overwrite` |

`vibestudio --cli cli commands` lists every command; see [Command line](manual/cli.md).

</details>

### Place copied objects

**Paste Map Content** (Ctrl+V) follows your profile: TrenchBroom places at the
cursor; other profiles retain the saved coordinates. **Paste at Original
Position**, **Paste at Cursor** and **Paste to Camera** are always available in
**Edit**, search and **Tools > Clipboard**. TB uses Ctrl+Alt+V for original
position; NRC uses Shift+V for camera placement. User key overrides take precedence.

Cursor placement uses the active plan's reference depth or the brush under the
camera pointer. Outside the pane it uses the pane centre. A camera-ray miss uses
a plane 256 units ahead in perspective. In the default profile's orbit view, a
miss stays on the focus plane. **Paste to Camera** centres the assembly using a
snapped translation towards the camera, or the visible focus-plane centre in an
orbit view. Point entities use class bounds, or a 16-unit marker when unknown.
**Paste with Offset…** retains the numeric preview workflow.

To move existing objects to the camera instead, use **Move to Camera** (**Edit** > **Duplicate and Move**, **Tools** >
**Duplicate and Move**, or <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>V</kbd> in the NetRadiant Custom profile). It uses the
**Paste to Camera** rule on the selection: its centre goes to the camera, or the orbit view's focus centre, by a
translation snapped to the grid while **Snap** is on, as one undo step. The objects keep their identities, order and
place in the file, and the clipboard is left alone. In **Edit Vertices**, **Edit Edges** or **Edit Faces** it moves
the picked components. A selection already at the camera stays put. From a script, use `map move --camera`.

These commands preserve ownership, native groups, asset paths and patch UVs,
follow Texture Lock and **Create in**, and add one undo step. The cancellable
worker rejects invalid or stale placement without changing the map. CLI
`map paste` offers camera, plane and ray targets plus `--context`; see
[Clipboard placement](LEVEL_PASTE.md) for exact snapping, limits and remaining
compatibility gaps. **Status: Partial.**

### Learn more

- [Editor profiles and controls](manual/editor-profiles.md)
- [Level editor design and acceptance notes](LEVEL_EDITOR.md)
- [Scene layers, groups and locks](LEVEL_SCENE.md)
- [Placed model appearances](LEVEL_MODEL_APPEARANCE.md)
- [Map format support](SUPPORT_MATRIX.md)


#### Copy and paste face attributes

In the TrenchBroom profile, **Copy Map Selection** (Ctrl+C) copies picked faces.
Without picks, it copies objects. In any profile, **Copy Surface** copies the
picked component faces in Face mode, or the inspected face in object mode.
Normal **Paste Map Content** (Ctrl+V) recognises face text and applies its last
face to picked faces. In object mode only, it uses all selected brush faces when
no faces are picked. Empty component picks and Vertex/Edge mode never expand a
surface edit to whole brushes. See [component-face targets](manual/levels.md#edit-surfaces-from-component-faces).
Geometry and content flags stay in place; one **Undo** restores the batch.
TB face picks cannot be cut.

Face paste transfers material, offsets, rotation, scales and explicit surface
flags/value. Omitted flags preserve existing values. Valve targets keep their
own UV basis and rotate it to the requested angle. **Surfaces > Face attributes**
provides the same mode for the sampled session source. Sampling alone leaves
system clipboard text unchanged. Matrix/non-matrix conversion and patch targets
need another transfer mode. See [Clipboard placement](LEVEL_PASTE.md#face-clipboard)
for limits, cancellation and CLI equivalents.

#### Copy and cut objects

**Copy Map Selection** keeps the owner properties of selected brush-entity
children and copies only those children. **Cut Map Objects** copies whole
objects and removes them as one undo step. Finish component/clipper tools before
cutting. Locks, cancellation, stale selection or clipboard changes preserve the
map; large copies show progress with **Cancel**. Model, sound and texture paths
stay connected to the normal dependency/build/package workflow. See
[object clipboard controls and limits](LEVEL_PASTE.md#copy-and-cut-objects).


### Select by hierarchy or source line

**Partial:** these commands work on Quake, Quake II and Quake III text maps.
Finish component editing, face picking or clipping before using them.

**Select Siblings** replaces the selection with selectable objects sharing its
parents. A brush inside a brush entity selects that entity's other brushes and
patches. A world brush selects the other objects in its layer, including children
of brush entities. Closed groups stay together; an open group limits selection
to its contents. Selecting objects with several parents includes each parent's
children once. Layers remain separate.

In the **TrenchBroom** profile, **Ctrl+B** selects siblings and **Ctrl+Alt+B** runs
the compiler profile. Other profiles expose the command without a default key.
User shortcut overrides continue to take precedence.

**Select by Source Line Number…** accepts positive, one-based line numbers
separated by commas or spaces. A brush face line selects its brush; a patch line
selects its patch. A brush-entity property or bracket line selects its selectable
children. When a requested line also matches a selectable child, only matching
children are selected. Point-entity lines select that entity. The worldspawn
header and comments between objects select nothing. Valid numbers with no match
clear the selection; invalid input shows an error before running the query.
Localised decimal digits, including Arabic/Indic digits, are accepted.

Line numbers refer to the **source text at last open**, as do object diagnostics.
Edits and saves preserve this reference. New or copied objects acquire source
positions when you save and reopen the map. Use the same map revision as a
compiler diagnostic, or save and reopen before looking up its new line numbers.
Deleting an object removes it from these results; Undo restores its source
identity. Closed groups expand a matching member to the whole group. VibeStudio
scene metadata has no group-bracket line, and foreign TB group/layer metadata
conversion is still incomplete.

Both commands respect hidden, locked and filtered objects and the current open
group. A closed group containing an excluded member is excluded as a whole.
If a grouped entity owns geometry explicitly assigned outside that group, the
group is also excluded; regroup its contents before selecting it as one unit.
Selection changes leave saved map contents untouched. TB records one selection
undo step; NRC and default profiles keep selection outside history. Preparation
is cancellable, and changes to the map, selection, tool or filters discard a
stale result. These commands do not provide Doom WAD/UDMF source-line selection
or face/component selection.

The CLI prints stable selectors for subsequent authoring commands:

```sh
vibestudio --cli map select-siblings maps/start.map --object brush:12 --json
vibestudio --cli map select-lines maps/start.map --line "42, 57" --line 80 --json
vibestudio --cli map select-lines maps/start.map --line 42 --context GROUP-ID --exclude brush:4 --json
```

Repeat **--object**, **--line** or **--exclude** as needed. The two query modes
require their own input option. JSON returns `selection`, `source`,
`sourceSha256`, `context`, `queryOnly: true` and `mapWritten: false`;
source-line queries also return sorted, unique `lines` and
`lineBasis: "loaded-source"`. No file is written and no selection session is
persisted. **--output**, **--overwrite** and **--dry-run** are rejected.

### Edit surfaces from component faces

**Status: Partial.** In **Edit Faces**, the picked brush-face components are the
targets for both **Surfaces > Selection** and **Surfaces > Picked Faces**.
Quick adjustments, **Paste Surface**, plain face-record paste and **Surface
Alignment…** use exactly those faces, including faces of brushes selected
through an entity. Patch control-point picks remain intact and do not become
whole-patch surface targets. The camera highlights the picked brush faces as
you change the selection; leaving Face mode restores whole-object highlights.
Empty face picks and **Edit Vertices/Edges** have no component-derived surface
targets. Independent **Select Textured** faces and query-selected whole patches
remain additional surface targets in every mode; see
[textured surfaces](manual/levels.md#select-textured-surfaces). Leave component mode to edit all
whole selected objects.

**Copy Surface** exports the picked component faces as ordered face records and
keeps the last face in the internal surface clipboard. TB's ordinary **Copy**
also uses picked faces; other profiles retain their ordinary object-copy
policy. In object mode, the existing camera/Inspector face targets and whole
brush/patch Selection target still apply. Entering component mode clears old
camera face picks so they cannot silently return when the tool is left.

Surface changes preserve object/component picks and create one content undo
step. Changing component picks or mode cancels queued work and invalidates an
alignment preview. Active geometry gestures block new surface edits. A
selection-inclusive Radiant paste retains the distinction between the hit face
(which receives copied flags) and additional selected faces (which retain their
flags). The shared surface service accepts an explicitly empty captured
selection without expanding it to whole objects.

For automation, **map select-type --mode faces** returns `surfaceTargets` such
as `face:0:2` alongside its zero-based component `selectors`. Each surface target
can be passed directly to **map copy-surface**, **map paste-surface** or material
painting as **--target**. Surface target face numbers are one-based. Queries
remain read-only; authoring commands require an explicit output as usual.

### Step textures by Radiant increments

**Status: Partial.** The **Surfaces** tab's **Adjust** section can step a
texture the way Radiant's Surface Inspector does, on the same target as the
other quick adjustments: the selection, **Picked Faces**, or the picked face
components in Face mode.

- **Rotate about** chooses where **Rotate −** and **Rotate +** turn by the
  **Rotation step**. **Face centre** keeps the middle of each face's texture in
  place. **Texture origin** keeps the offsets, so the texture turns about the
  point where the world origin meets the face's plane (coordinate 0,0 on a
  patch).
- **Scale U −**, **Scale U +**, **Scale V −** and **Scale V +** subtract the
  **Scale step** (0.5 to start with) from each face's texture scale, or add it,
  keeping the texture origin in place. A scale that would reach zero flips its
  sign instead, which mirrors the texture. Brush-primitive faces read their
  scale from the texture matrix, so they need the material's image size from
  the open package. Patches have no scale value: **+** multiplies their texture
  coordinates by the step and **−** divides them by it, as NetRadiant Custom
  does, so a step above 1 works the other way round.

Each command, or a quick burst of them, is one undo step. Hidden objects are
not targets, and a brush in a locked layer refuses the step, with the reason on
the **Surfaces** tab.

The NetRadiant Custom profile starts the **Rotation step** at 45° about the
**Texture origin** and uses that editor's keys: <kbd>Shift</kbd>+<kbd>Page Down</kbd>
for **Rotate Texture −**, <kbd>Shift</kbd>+<kbd>Page Up</kbd> for **Rotate Texture +**,
and <kbd>Ctrl</kbd>+arrow keys for **Texture Scale U −** and **U +** (left and
right) and **V −** and **V +** (down and up). While there is a target these keys
come before the 2D view's <kbd>Ctrl</kbd>+arrow panning; with nothing targeted they
pan as before. Other profiles start at 15° about each **Face centre** and leave
the scale steps without keys. Choosing a profile resets both values; change
them on the tab whenever you like.

Turns follow NetRadiant Custom's direction for each face format. Classic and
Valve 220 faces turn as their stored rotation does; brush-primitive faces always
turn anticlockwise, seen from the front, for **Rotate +**.

On the command line, **map align-textures --scale-step U,V** adds the
increments, and **--degrees N --anchor origin** turns about the texture origin
(**--anchor centre** is the default):

```sh
vibestudio --cli map align-textures room.map --object brush:12 --scale-step 0,0.5 --output scaled.map --json
vibestudio --cli map align-textures room.map --object brush:12 --degrees 45 --anchor origin --output turned.map
```

### Move and flip textures from the camera

**Status: Partial.** These commands move and mirror textures the way the 3D
camera shows them, as TrenchBroom's camera texture keys do, rather than along
each face's own U and V. They are in the **Surfaces** menu under **Move
Textures from Camera**, on the **Tools** tab and in the command palette:

- **Move Textures Up**, **Down**, **Left** and **Right** move the texture one
  grid size, in texels, up, down, left or right on screen.
- The **(Coarse)** forms move twice the grid size; the **(Fine)** forms move
  one texel, whatever the grid.
- **Flip Textures Horizontally** mirrors the texture axis that runs across the
  view, and **Flip Textures Vertically** the one that runs up it, about the
  middle of each face, as **Flip U** and **Flip V** do.

For each face, the texture axis that runs most nearly across the view counts
as horizontal, so a turned or mirrored texture still moves the way you see it,
on classic, Valve 220 and brush-primitive faces and on patches. A patch uses
the direction its texture runs at its middle control point.

The commands act on the faces you picked in the camera or with **Select
Textured**. With no face picked they act on the **Surfaces** tab's target, as
the other quick adjustments do. Each command, or a quick burst of them, is one
undo step. Brush-primitive faces and patches need the material's image size
from the open package to move; flips need none. Hidden objects are not
targets, and a locked layer refuses the edit, with the reason on the
**Surfaces** tab.

The TrenchBroom profile gives them its keys in the camera: the arrow keys
move by the grid, <kbd>Shift</kbd>+arrows coarsely, <kbd>Ctrl</kbd>+arrows
finely, <kbd>Ctrl</kbd>+<kbd>F</kbd> flips horizontally and
<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F</kbd> vertically. These keys move
textures only while a face is picked and you are not editing vertices, edges
or faces; otherwise they still nudge, copy and flip the selection. Other
profiles leave the commands without keys; give them your own in
**Help** > **Keyboard Shortcuts**.

On the command line, **map align-textures --camera-shift RIGHT,UP** and
**--camera-flip horizontal|vertical** do the same for a camera named by
**--camera-yaw** and **--camera-pitch**:

```sh
vibestudio --cli map align-textures room.map --face 12:3 --camera-shift 16,0 --camera-yaw 90 --output moved.map --json
```

### Rotate textures by fixed steps

**Status: Partial.** These commands turn textures by a fixed angle, as
TrenchBroom's texture rotation keys do, whatever the **Surfaces** tab's
**Rotation step** says. They are in the **Surfaces** menu under **Rotate
Textures**, on the **Tools** tab and in the command palette:

- **Rotate Textures Clockwise** and **Rotate Textures Counter-clockwise** turn
  the texture 15°.
- The **(Coarse)** forms turn it a quarter turn, 90°; the **(Fine)** forms
  turn it 1°.

Each texture turns the named way as you see the face from the front, the side
the camera shows, on classic, Valve 220 and brush-primitive faces and on
patches, whether the texture reads the right way round or mirrored. The turn
keeps the texture's offsets, so it pivots about the texture origin rather
than the middle of the face; use **Rotate −** and **Rotate +** with **Face
centre** to keep the middle in place.

The commands act on the faces you picked in the camera or with **Select
Textured**, or with no face picked on the **Surfaces** tab's target. Each
command, or a quick burst of them, is one undo step. Patches need the
material's image size from the open package; faces need none. Hidden objects
are not targets, and a locked layer refuses the turn, with the reason on the
**Surfaces** tab.

The TrenchBroom profile gives them its keys in the camera: <kbd>Page Up</kbd>
turns clockwise and <kbd>Page Down</kbd> counter-clockwise, with
<kbd>Shift</kbd> for the coarse turns and <kbd>Ctrl</kbd> for the fine ones.
Like the texture moves, these keys turn textures only while a face is picked
and you are not editing vertices, edges or faces; otherwise they still nudge
and copy the selection along the depth axis. Other profiles leave the commands
without keys.

On the command line, **map align-textures --clockwise N** turns N degrees
clockwise, or counter-clockwise for a negative N:

```sh
vibestudio --cli map align-textures room.map --object brush:12 --clockwise 15 --output turned.map --json
```

### Reset textures

**Status: Partial.** Two commands reset a texture's mapping in one pass,
whatever format each face is written in. They are on the **Surfaces** tab as
**Reset** and **Radiant Reset**, in the **Surfaces** menu, on the **Tools** tab
and in the command palette:

- **Reset Texture Alignment** gives every face no shift and no rotation at
  the default scale (0.5 in Quake III, 1 otherwise) and keeps its kind, as
  TrenchBroom's reset does: classic faces get their natural numbers, Valve 220
  and brush-primitive faces are laid in their own plane, as **Align to Face**
  lays them, and patches get their natural spread.
- **Radiant Texture Reset** resets as NetRadiant Custom's **Reset Texture**
  does: classic faces get their natural numbers, Valve 220 and
  brush-primitive faces take their plane's own basis, and patches are capped,
  projected along their average normal. On floors and ceilings the plane's
  basis runs a quarter turn from **Align to Face**.

Unlike **Align to Face**, neither refuses a sloped classic face, so a
selection that mixes formats needs one pass. Both act on the faces you picked
in the camera or with **Select Textured**, or with no face picked on the
**Surfaces** tab's target, as one undo step. Brush-primitive faces and
patches need the material's image size from the open package. Hidden objects
are not targets, and a locked layer refuses the reset.

The TrenchBroom profile binds <kbd>Shift</kbd>+<kbd>R</kbd> to **Reset Texture
Alignment** and <kbd>Shift</kbd>+<kbd>Alt</kbd>+<kbd>R</kbd> to **Natural
Texture Mapping**, its reset to world-aligned textures. The NetRadiant Custom
profile binds <kbd>Shift</kbd>+<kbd>N</kbd> to **Radiant Texture Reset**.

#### Set texture values across the map

**Reset Textures…**, in the **Surfaces** menu and on the **Tools** tab, sets
absolute values, as bobToolz's window of that name does:

1. In **Apply to**, choose **Selection** (the **Surfaces** tab's target, or
   the picked faces) or **Whole map**: every visible, unlocked surface in the
   open group. What the views hide stays as it is.
2. Tick **Only faces using** and name a texture to change only the surfaces
   that use it. Case and a leading `textures/` do not matter.
3. Tick **Rename to** to give the surfaces another texture.
4. Tick each of **Scale U**, **Scale V**, **Shift U**, **Shift V** and
   **Rotation** you want to set, and give its value. Unticked values stay as
   they are.
5. Choose **Reset**.

Each face keeps its format. Valve 220 faces keep their axes for a new scale
and turn them by the change of rotation. Brush-primitive faces take the values
as Radiant's Surface Inspector reads such a face, which needs the material's
image size. Patches store no scale, shift or rotation, so they take only the
new name. The whole reset is one undo step; a locked target refuses it.

On the command line, **map align-textures** takes **--reset-alignment U,V** and
**--radiant-reset U,V**, and the Reset Textures values as **--set-scale-u**,
**--set-scale-v**, **--set-shift-u**, **--set-shift-v**, **--set-rotation**
and **--rename**, for the **--object** selection or the **--whole-map**,
optionally **--only-texture NAME**:

```sh
vibestudio --cli map align-textures room.map --object brush:12 --radiant-reset 0.5,0.5 --output reset.map --json
vibestudio --cli map align-textures room.map --whole-map --only-texture base_wall/concrete --rename base_wall/metal --set-rotation 0 --output retextured.map
```

### Fit and project textures

**Status: Partial.** These commands fit a texture to a face without
stretching it, and lay one flat projection across several surfaces, as
NetRadiant Custom's Surface Inspector does. They are on the **Surfaces** tab,
in the **Surfaces** menu, on the **Tools** tab and in the command palette.

To fit a texture and keep its proportions:

- **Fit Texture Width** (**Fit Width** on the **Surfaces** tab) fits one
  repeat across each face's width and scales its height by the same amount,
  starting it at the face's edge.
- **Fit Texture Height** (**Fit Height**) does the same down the height.
- **Fit Texture Width Only** and **Fit Texture Height Only** (**Fit U** and
  **Fit V**) fit one axis and leave the other as it is.

For other repeat counts, use **Surface Alignment…**: choose **Fit**, set the
axis to keep to 0 and tick **Keep proportions**.

To project a texture, use the **Project texture** buttons. Each lays the
default texture, with no shift or rotation at the default scale, flat across
the surfaces:

- **Project Texture Axially** (**Axial**) projects each face along the world
  axis it faces most, as **Natural** does, and each patch along the axis
  nearest its facing, rather than spreading the texture along the patch.
- **Project Texture from 2D View** (**2D View**) projects straight from the 2D
  view you used last, so every surface shares one projection, reading the
  right way round from that view.
- **Project Texture from Camera** (**Camera**) projects along the 3D camera's
  view in the same way.

The fits act on the **Surfaces** tab's target. The projections act on the
faces you picked in the camera or with **Select Textured**, or with no face
picked on the **Surfaces** tab's target. Each command, or a quick burst of
them, is one undo step. **Project Texture from Camera** keeps using the
camera's last direction when its pane is hidden. The fits need the material's image size from the open
package, except on brush-primitive faces and patches; the projections need it
on brush-primitive faces and patches. Hidden objects are not targets, and a
locked layer refuses the edit, with the reason on the **Surfaces** tab.

A projection leaves some surfaces as they were, and the **Surfaces** tab says
how many: faces it runs along (the walls, when you project from the **Top**
view), and classic faces whose format cannot hold it. A classic face holds a
projection only where it suits the face's own axis, such as the **Top** view's
on a floor, whereas NetRadiant Custom leaves every classic face alone. When no
targeted surface can take the projection, nothing changes and the tab says
why. Unlike NetRadiant Custom, brush-primitive faces and patches take the same
projection as Valve 220 faces, which reads the right way round, and **Fit
Width** keeps a patch's proportions rather than re-tiling it to one repeat.

NetRadiant Custom has no default keys for these commands, so no profile binds
them; give them your own in **Help** > **Keyboard Shortcuts**.

On the command line, **map align-textures --fit U,V --keep-proportions**,
**--project axial|x|y|z|-x|-y|-z|camera** and **--project-direction X,Y,Z** do
the same:

```sh
vibestudio --cli map align-textures room.map --object brush:12 --fit 1,0 --keep-proportions --package ./assets --output fitted.map
vibestudio --cli map align-textures room.map --object brush:12 --project z --output projected.map --json
```

### Edit texture coordinates in the UV Editor

**Status: Partial.** The **UV Editor** shows one brush face's outline, or one
patch's control grid, laid over its texture, so you can drag the texture into
place by eye, as NetRadiant Custom's TexTool plugin does. It works with
classic, Valve 220 and brush-primitive faces and with Quake III patches.

To edit a face's texture:

1. Pick one face in the camera or in Face mode, or select one brush and choose
   its face in the **Inspector**. For a patch, select just that patch.
2. Choose **UV Editor…** in the **Surfaces** menu, on the **Tools** tab, from
   the right-click menu or in command search.
3. Drag any numbered point. The whole texture follows it, and the point lands
   on the **Snap** grid. When you let go, that point becomes the **anchor**,
   drawn as a ringed cross.
4. Drag another point to turn the texture about the anchor. Hold
   <kbd>Shift</kbd> to turn in **Turn step** increments. Drag the anchor
   itself, or inside the face, to move the texture again.
5. Check the preview beside the view, then choose **Apply**. **Cancel** or
   <kbd>Esc</kbd> leaves the map as it was.

A patch's control points move one at a time: drag a point, or drag across
several to select them, holding <kbd>Ctrl</kbd> to add, and drag one of the
selected points to move them together.

The view's keys work while it has keyboard focus:

| Key | Action |
| --- | --- |
| Arrow keys | Nudge by the snap step (<kbd>Shift</kbd>: eight steps). A face moves as a whole; a patch moves its selected points, or all of them. |
| <kbd>[</kbd> and <kbd>]</kbd> | Turn anticlockwise or clockwise by the **Turn step**, about the anchor or else the centre. <kbd>{</kbd> and <kbd>}</kbd> turn by one degree. |
| <kbd>N</kbd>, <kbd>Shift</kbd>+<kbd>N</kbd> | Step to the next or previous point. |
| <kbd>A</kbd> | Make the current point the anchor, or clear it. |
| <kbd>Ctrl</kbd>+<kbd>A</kbd> | Select every patch point. |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd> | Undo or redo inside the editor. |
| <kbd>+</kbd>, <kbd>-</kbd>, <kbd>Delete</kbd>, <kbd>Insert</kbd>, the wheel | Zoom (<kbd>Delete</kbd> in and <kbd>Insert</kbd> out, as in TexTool). |
| <kbd>Ctrl</kbd>+arrow keys, right- or middle-drag | Pan. A right-click without dragging opens a menu. |
| <kbd>Home</kbd> or <kbd>F</kbd> | Frame all the points. |
| <kbd>Enter</kbd> | **Apply**. |

The **U** and **V** cells in the point list move a point to exact texel
coordinates; on a face, the whole texture follows. The buttons beside the view
do the same as the keys.

Things to know:

- **Apply** makes every change you made in the editor one undo step. Hidden
  objects, locked layers and objects outside the open group cannot be edited.
- Coordinates are in texels, with U to the right and V down the image. A move
  changes only the face's offsets. A Valve 220 face keeps its scale values, and
  a brush-primitive face turns exactly.
- A classic face whose texture is scaled differently along U and V cannot hold
  every turn. It takes the nearest classic mapping, keeping the anchor in
  place, and the status says so. Tick **Convert to Valve 220 when a turn needs
  it** to keep the turn exactly.
- Brush-primitive faces and patches count their coordinates in repeats, so
  they need the image's size. Without an open package, **Use explicit texture
  size** is ticked at 64 × 64 texels; change it before you edit. Classic and
  Valve 220 faces show a checker when the image is not found.
- The preview shows the face's brush, or the patch, with the edited texture.
  The camera and plan views update when you choose **Apply**.

No profile binds a key to the **UV Editor**, because neither NetRadiant Custom
nor TrenchBroom does; give it one in **Help** > **Keyboard Shortcuts**.

On the command line, `map edit-uv` lists a face's or patch's points in texels
and moves or turns them through the same service:

```sh
vibestudio --cli map edit-uv base.map --target face:3:1 --json
vibestudio --cli map edit-uv base.map --target face:3:1 --rotate 15 --anchor 1 --output base-turned.map
vibestudio --cli map edit-uv curves.map --target patch:0 --move-point 5 --to 32,16 --texture-size 128,128 --output curves-uv.map
```

### Set face flags

**Status: Partial.** Quake II and Quake III brush faces carry content flags,
surface flags and a surface value after their texture. The **Flags** section
of the **Surfaces** tab shows and changes them, as NetRadiant Custom's Surface
Inspector and TrenchBroom's Face Inspector do.

To change a face's flags:

1. Pick faces in the camera, or select brushes. With no face picked, the
   section follows the **Surfaces** tab's target.
2. Open the **Surfaces** tab and find **Flags**. Each flag has a box named as
   the game names it, such as **Light** or **Player clip**. A ticked box is set
   on every targeted face; a partly ticked box is set on some of them.
3. Tick or clear a box. A partly ticked box ticks on every face. Only that flag
   changes; each face keeps its other flags.
4. To set the surface value, such as a Quake II light's brightness, type it in
   **Value** and press <kbd>Enter</kbd>. **Value** is empty when the faces'
   values differ.

**Clear Flags** (**Clear Face Flags** in the **Surfaces** menu, on the
**Tools** tab and in command search) clears every flag but detail and sets the
value to 0, as NetRadiant Custom's **Unset** does.

Each change is one undo step, and the boxes follow **Undo**. The detail flag is
shown but cannot be changed here, because it belongs to whole brushes: use
**Make Detail** or **Make Structural**. Bits the game leaves unnamed are kept,
and the section says when any are set. Hidden objects are not targets, and a
locked layer or a face outside the open group refuses the change, with the
reason on the **Surfaces** tab. Quake maps have no face flags, so the section
has no boxes there. A Quake II map whose faces write no flag numbers yet is
still recognised by its header or its Quake II classes, and only the faces you
change gain the numbers. Texture folders alone do not identify the game for
flag edits: modern Quake maps use them too. Changed flags are written as
signed numbers, as NetRadiant Custom and TrenchBroom write them, so
a flag in the top bit, such as **N64 scroll flip** or **No drop**, appears as
a negative number that every compiler reads. On Quake III maps the compiler reads only the detail flag
from a face, and shader surfaceparms decide the rest in game; the section
says so.

On the command line, `map face-flags` reads and changes the same flags by name:

```sh
vibestudio --cli map face-flags base.map --face 3:1 --json
vibestudio --cli map face-flags base.map --object brush:3 --set-content playerclip --output base-clip.map
```

### Select textured surfaces

**Status: Partial.** **Select Textured** in **Edit**, the viewport **Select**
menu, **Tools > Select**, the object context menu and command search uses the
current material. The NRC profile assigns **Ctrl+Shift+A**; other profiles keep
their existing bindings. The read-only **map select-textured** CLI uses the
same query service. Matching materials do not select every parent brush.
Existing object and independent face picks are retained; matching faces and
whole patches are added. Matching ignores case but preserves whitespace. No
match leaves the supplied picks unchanged. Manipulation handles and their mode
are separate from these surface picks.

New results respect hidden objects, view exclusions, locks and the current
open group. Open a closed group before querying its individual surfaces. This
is a deliberate studio boundary: NRC's native brush-face walker does not apply
the same visibility filtering. Existing hidden picks can remain in query
results; an authoring workflow must check their editability before applying
changes. Queries return the complete result or fail; they do not truncate
results beyond 16,384 combined face and matching patch targets.

```console
vibestudio --cli map select-textured maps/start.map --material stone --json
vibestudio --cli map select-textured maps/start.map --material stone --object entity:4 --face face:12:1 --json
```

Use the returned `surfaceTargets` with the normal material or surface commands.
The source hash and ordered brush-geometry fingerprints identify the queried
snapshot. Re-query after source or geometry changes. Surface-only changes do
not alter the geometry fingerprint; face reordering, changed planes or changed
ownership do. See [CLI query details](manual/cli.md#query-textured-level-surfaces).

Plan views draw independent faces with dashed outlines; the camera highlights
their triangles. The viewport readout and accessible descriptions show the
independent face count. **Surfaces** shows the complete target counts. Existing
vertex, edge, face and patch-point picks keep their exact identities and mode.
If adding matching patches would exceed the current component handle capacity,
the complete query is refused; leave component mode or reduce the object picks.

In object mode, **Surfaces > Selection** includes the selected objects and
independent faces. In component mode it includes picked brush-face components,
independent faces and the whole patches selected by this query. Vertex and edge
picks alone do not provide surface targets. **Picked Faces**, **Copy Surface**,
surface alignment and face-attribute paste use the exact face identities.
**Apply Texture** uses the same selection targets. Surface-only edits and their
undo preserve independent face picks, and transforming the picked faces keeps
them on the moved faces. Other changes to geometry or ownership, hiding,
locking, leaving the group boundary and reloading retire them. Undoing such a
change does not revive stale face indices.

**Select None**, or **Escape** in a viewport with independent face picks, first
clears surface and component picks while keeping whole objects. The next clear
leaves component mode if it is active; a further clear removes whole objects.
An active viewport gesture keeps its normal cancellation priority. Query-picked
patches remain whole-object picks until deselected. The existing **Select by
Texture** command continues to select whole objects.

Independent face picks do not become handles, but in **Edit Faces** mode they
move, rotate, scale and shear with the face handles; see
[Transform independently picked faces](manual/levels.md#transform-independently-picked-faces).
Native input, screen-reader behaviour, cross-platform acceptance and
real-project use still need production review.

### Find, replace and reveal textures

**Status: Partial.** **Find and Replace Textures…** (**Edit**, **Tools** >
**Surfaces**, the **Surfaces** menu above the views, the object right-click
menu and command search) opens a window that stays open while you work, as
NetRadiant Custom's Find / Replace window does.

1. Type or choose the texture in **Find**. Picking a tile on the **Textures**
   tab, or an image on the **Textures** surface, fills the field that last had
   focus: **Find** until you click into **Replace with**. The window says which
   field a pick fills. While it is open, <kbd>Enter</kbd> or a double-click on
   a tile fills the field instead of applying the texture.
2. Choose where to look in **Search in**: **Whole map**, **Selected objects**
   (selected brushes and patches, those of selected entities, and Doom lines
   and sectors) or **Picked faces** (faces picked by **Select Textured**,
   **Find** or in the camera, with the whole patches **Select Textured** picked).
3. Read the line under the fields: it counts what will change as you type.
4. Choose **Find** to pick the matching faces and select the matching patches,
   adding to the current picks as **Select Textured** does; on a Doom map it
   selects the lines and sectors instead. Leave **Replace with** empty and
   <kbd>Enter</kbd> finds. Choose **Replace** to change every matching surface
   in scope as one undo step. The window stays open for the next pair;
   **Close** or <kbd>Escape</kbd> hides it.

Hidden objects, view filters, locked objects and anything outside an open
group keep their textures; the count and the status bar say how many uses were
left. Members of a closed group are replaced with the rest of the map, but
**Find** leaves them out until you open the group, as **Select Textured** does.
Replacing keeps each surface's alignment, and face picks survive it.

**Reveal Material** (**Edit**, **Tools** > **Surfaces**, the **Surfaces** menu
and the object right-click menu) shows the selection's material on the
**Textures** tab: it selects and scrolls to the tile, clears a filter that hides
it and makes it the current material for painting and new brushes, as
TrenchBroom's Reveal in texture browser does. Picked faces count first;
otherwise the selected brushes, patches, entities, Doom lines and sectors.
When they use more than one material the command is unavailable, and its
tooltip and status tip name two of them. Neither upstream editor gives these
commands a key; add your own in **Help** > **Keyboard Shortcuts**.

```console
vibestudio --cli map replace-texture maps/start.map --from base/wall --to base/metal --face face:3:1 --face face:3:4 --output maps/start-metal.map
vibestudio --cli map select-textured maps/start.map --material base/wall --object entity:2 --within-selection --json
```

**map replace-texture** takes `--object` objects or `--face` picks; its JSON
reports `scope`, `targets` and `skipped`. See
[Find and replace textures](manual/cli.md#find-and-replace-level-textures). Native
physical input, screen-reader behaviour and macOS and Linux acceptance still
need review.

### Select all of a type

**Status: Partial.** **Select All of Type** is available for Quake-family text
maps in **Edit**, the viewport **Select** menu, **Tools > Select**, the context
menu and command search. NRC uses **Shift+A**. **Select All of This Class**
remains a separate command with its existing behaviour in every profile.

In object mode, the command replaces the current picks with every matching
entity class supplied by the selection. Selected brushes and patches supply
their owner's class; worldspawn supplies no class. Several selected owners can
supply several classes. Class keys and values match exactly, including case
and whitespace. Matching entities include their selectable brush and patch
children, even when the entity has no drawable origin.

When there is no non-world entity class, the command selects brushes and
patches using the **current material** from the texture browser or material
picker. Selecting a world brush, a world patch or nothing therefore uses this
material fallback. Material matching ignores case but keeps whitespace.
A query with no matches clears the selection.

In **Face** mode, the command replaces component picks with matching faces on
selected brushes, including brush geometry represented by selected entities.
It keeps object picks, clears patch-point picks, and creates no map undo step.
A material with no matching faces clears component picks. **Vertex** and
**Edge** modes leave selection unchanged; the GUI disables the action there.
Select fewer objects if the current component handles are truncated. A brush
with invalid geometry cannot contribute a partial face result.

These face picks feed component geometry tools and the shared
[surface-edit and clipboard targets](manual/levels.md#edit-surfaces-from-component-faces).

New results respect hidden objects, filters, locks and the open group. Closed
groups are atomic in object mode; a group with an excluded child is skipped.
An ungrouped entity with an excluded child contributes only permitted children,
without its whole-owner selector. Open a closed group before selecting its
faces. Hidden original picks still supply class names, but do not remain in the
replacement selection unless they are visible results.

Object changes follow the profile's history policy: TB records a non-content
selection step, while NRC and default profiles keep selection outside history.
Unchanged results add no step. Preparation is cancellable; changes to the map,
picks, components, material, filters or history policy discard pending results.
Native map bytes, asset references and compiler/package inputs stay unchanged.

```sh
vibestudio --cli map select-type maps/start.map --object brush:12 --object entity:4 --json
vibestudio --cli map select-type maps/start.map --material stone --json
vibestudio --cli map select-type maps/start.map --mode faces --object entity:2 --material stone --json
```

The CLI accepts repeated **--object**, **--exclude** and an optional **--context**
group. **--material** is required for fallback and face queries; class queries
do not need it. **--mode** defaults to **objects**. The **vertices** and **edges**
modes report an unchanged selection. JSON includes `selection`, `matchedClasses`,
`materialFallback`, `componentSelectionChanged`, `sets`, `selectors`,
`componentCount`, `surfaceTargets`, `sourceSha256`, `queryOnly: true` and `mapWritten: false`.
Face selectors such as `brush:12/face/0,3` use the same form as component
transforms. A false `componentSelectionChanged` means retain existing component
picks. No file or selection session is written; write options and **--line**
are rejected. Empty or missing exact `classname` values are treated as an empty
class, rather than triggering material fallback.

### Select connected entities

**Status: Partial.** In a Quake-family text map, **Select Connected Entities**
adds direct incoming and outgoing `target`/`targetname` connections to the
current object selection. Select an entity, brush or patch; brushes and patches
supply their owner's names. The command is in **Edit**, the viewport **Select**
menu, **Tools > Select**, the context menu and command search. NRC uses
**Ctrl+Shift+E**; TB keeps that key for **Select Complete Tall**.

The original picks keep their order. Incoming matches are added first, then
outgoing matches, without duplicates. Names and keys match exactly, including
case and whitespace. Only the names selected at the start are used: repeat the
command to advance another hop. `killtarget` and other game-specific reference
keys are not part of this NRC command. Entities without an origin or drawable
anchor can match. Worldspawn contributes no names and is never added.

Matching entities include their selectable brush and patch children. New
results respect hidden objects, locks, filters and the open group. Closed groups
stay together; a group with an excluded member is skipped. If one child is
excluded, an ungrouped entity contributes its permitted children without adding
the owner that would implicitly select the excluded child. Existing hidden
picks remain selected and still supply connection names.

The command also works on the object selection while vertex, edge or face mode
is active. Finish clipping or an active drag/stroke first. It uses cancellable
preparation; changes to the map, picks, filters or selection-history policy
discard pending results. TB records one non-content selection undo step, while
NRC and default profiles keep selection outside history. A query that adds
nothing creates no undo step. Saved map contents and compiler/package inputs
remain unchanged. Doom WAD/UDMF connected geometry uses its separate command.

```sh
vibestudio --cli map select-connected maps/start.map --object patch:2 --json
vibestudio --cli map select-connected maps/start.map --object entity:3 --context GROUP-ID --exclude brush:4 --json
```

Repeat **--object** and **--exclude** as needed. The read-only JSON response
contains `selection`, `originalSelection`, `added`, `connectionKeys`,
`transitive: false`, `sourceSha256`, `queryOnly: true` and `mapWritten: false`.
No map or selection session is written. **--line**, **--output**, **--overwrite**
and **--dry-run** are rejected. Separate **Select Sources** and **Select Targets**
currently use the drawing graph and have narrower coverage for patch owners
and entities without anchors; they are not aliases for this combined command.

### Select by brush volume

**Status: Partial.** Use **Select Inside**, **Select Touching** or a **Tall**
command from **Select** above the views, the context menu or command search.
The profile determines both the test and what happens to the source selection:

| Profile | Region test | Source objects |
| --- | --- | --- |
| VibeStudio default and other profiles | Combined selection bounds | Retained; replaces selection when matches exist |
| TrenchBroom | Each selected convex brush independently | Removed by Inside, Touching and Tall; one undo step |
| NetRadiant Custom | Each selected brush's axis-aligned bounds independently | Inside removes all selected objects; Touching retains them and adds matches |

A gap between separate selection brushes is excluded in the TB and NRC profiles.
TB requires only brushes; NRC requires at least one explicitly selected brush.
Open a closed group before using its brushes as volumes. Hidden, filtered, locked
and out-of-context targets are excluded. A closed group is tested as one bounds
unit; a partly hidden or locked group is excluded. Point entities use loaded
class bounds, with a 16-unit box for unknown classes. TB patches use a sampled
quadratic surface and grid-edge intersection; NRC uses their control-grid bounds.

**Select Tall** in TB projects each brush onto the active 2D view. The default and
NRC labels are **Select Complete Tall**. **Select Partial Tall** tests overlap of
the footprint. Both ignore depth; Partial Tall and NRC tall commands are
VibeStudio extensions. TB tall commands require a 2D view. TB shortcuts are
**Ctrl+E** (Inside), **Ctrl+T** (Touching) and **Ctrl+Shift+E** (Tall), subject to
shortcut overrides.

A consuming command removes its source volumes even when nothing matches.
**Undo** restores the objects, scene state and original selection together;
**Redo** restores the selected result. Finish component or clipper tools first.
Large queries run in the shared **Select by Region** progress dialogue; **Cancel
operation** keeps the map and selection unchanged. Changes to the map, selection,
view, profile, filters or entity definitions invalidate a pending result.
Save the edited map before building or packaging it.

The CLI uses the same service. A query never writes the source map:

```sh
vibestudio --cli map select-region maps/start.map --object brush:0 --object brush:4 --policy trenchbroom --mode inside --json
vibestudio --cli map select-region maps/start.map --object brush:0 --policy radiant --mode touching --json
vibestudio --cli map select-region maps/start.map --object brush:0 --policy trenchbroom --mode complete-tall --axis z --output maps/selected.map --dry-run --json
```

Remove **--dry-run** to write the result. Use **--overwrite** to replace an existing
output; the normal document writer checks conflicts and retains a backup.
**--policy bounds** is the default. Modes are **inside**, **touching**,
**complete-tall** and **partial-tall**; **--axis x|y|z** defaults to Z.
**--context** opens a scene group for the operation. **--definitions** loads custom definitions; otherwise the inferred game's built-in
catalogue supplies entity bounds. JSON **objects** lists new matches,
**selection** includes retained NRC Touching sources, **removedObjects** counts
selected sources consumed in the prepared result, and **queryOnly**, **dryRun**
and **mapWritten** distinguish preparation from publication. Selection is session
state; it is reported in JSON and is not stored in the saved .map.

Cross-platform native input, screen readers and dense real-project acceptance
remain unverified. NRC model-derived entity bounds, TB game-specific world-limit
clipping for Tall, and complete foreign group/filter semantics remain integration
gaps. Matching TB's grid-edge patch test does not provide continuous curved-surface
intersection. The default profile's empty query keeps its existing selection.

#### Apply a smart tag

**Partial.** Load game or project tags under **View** > **Filters** > **Tag File…**.
The command palette and **Edit** > **Smart Tags** then offer **Make &lt;tag&gt;**, and
**Make non-&lt;tag&gt;** for flag and classname tags. You can assign keys to these
commands in **Keyboard Shortcuts**; there are no default keys to conflict with
your editor profile.

Select brushes or patches, or pick individual faces. A texture tag applies a
matching material; a surfaceparm tag offers only loaded shaders declaring that
parameter. If several names match, choose one. Texture patterns can also name
an asset outside the open package. A classname tag needs whole objects: choose
a matching brush class, and the tag's optional `texture` is applied before the
brushes are tied to it. **Make non-&lt;tag&gt;** returns matching brush entities to
worldspawn; it does not restore an earlier texture.

Flag tags set or clear their named bits while retaining every other bit.
Detail requires whole brushes; patches have no face flags. Texture and
surfaceparm tags have no remove command: apply another material instead.
Hidden, filtered, locked and out-of-context targets are refused. Open a closed
group before editing individual faces inside it. Preparation is cancellable,
and each completed command is one undo step. Changes appear in the same map,
linked groups, compiler inputs and package workflow as other surface edits.
The GUI currently refuses the whole target brush when any face on it is
filtered, including a pick of a different, visible face on that brush.

`map smart-tag` provides the same edit and choice query from the CLI. Native
input and macOS/Linux acceptance testing remain open.

## Editor profiles and controls

An editor profile makes the **Levels** page answer the mouse and keyboard like a level editor you already know: which
button pans, how you select, how the camera moves, how the views are laid out and which keys do what. Pick one of 24
profiles, then adjust its gestures and keys to suit you.

> [!NOTE]
> **Status: Partial.** All 24 profiles change the Levels page's gestures, camera, keys and starting layout, and
> automated tests check each one. They are adaptations, not copies of the original editors: native tool modes,
> preference files and some upstream keys are not reproduced, and no profile has been tried with real input devices or
> screen readers yet.

### What a profile changes

- **2D views:** which buttons pan and zoom; how a click selects, adds or toggles; what a drag over empty space does
  (select an area, draw a brush or resize the selection); and, in the Radiant-style profiles, middle-click and arrow-key
  control of the 3D camera.
- **3D camera:** an orthographic view that orbits the map (VibeStudio Default) or a first-person camera with its own
  field of view; the look, orbit and pan drags; what the wheel does; and the keys that fly or drive the camera.
- **Material clicks:** Q3Radiant, GtkRadiant and both NetRadiant profiles sample, paint and paste materials with the
  middle button in the 3D view.
- **Keys:** the keys of Levels commands, such as <kbd>Space</kbd> to duplicate in the Radiant profiles. A profile can
  also take a command's key away when it needs that key for something else: TrenchBroom's <kbd>W</kbd> flies the
  camera, so **3D Wireframe** has no key there.
- **Layout and grid:** how the Levels views are arranged, the grid size you start with when you switch profile, and
  whether choosing or stepping the grid turns **Snap** on (NetRadiant Custom and NetRadiant do; set it per profile on
  the **Editing** tab).
- **Duplicate placement:** TB and NRC start copies at their source position. The
  optional nudge uses one grid step right and down in the active plan; configure
  it per profile on the **Editing** tab. NRC requires object mode for duplication.
- **Sidebars:** which side each Levels sidebar tab starts on, which is open first, and what the tabs are called, such
  as **Entity List** in the Radiant profiles and **Face Edit** in the Hammer ones. Each family of profiles remembers
  the tabs you move; see [Use the sidebars](manual/levels.md#use-the-sidebars). **Browse Editor Profiles…** shows both
  sidebars of the profile you are looking at.

### What a profile does not change

- **Your maps.** Every profile edits the same documents with the same undo, assets and build services. The Hammer
  profile does not add VMF files, and the DarkRadiant profile does not add Doom 3 maps.
- **The rest of the studio.** Other pages, panels and docks stay as they are. The **Models** page and the Mesh Editor
  have their own profiles, modelled on Blender, 3ds Max and MilkShape 3D (see
  [Choose your controls](manual/models.md#choose-your-controls)); they follow the **Blender Style** level profile until
  you choose one there.
- **Native modes and preferences.** Modal tools such as GtkRadiant's edge and vertex dragging, Doom Builder's V, L, S
  and T modes and the gizmos of the scene editors are not reproduced, and preference files from other editors cannot
  be imported. Each profile's block in the [profile reference](manual/editor-profiles.md#profile-reference) lists its gaps.
- **Your own keys.** Keys you set in **Help** > **Keyboard Shortcuts** always win over the profile's.

### Choose a profile

1. On the **Levels** page, choose **Controls** on the view toolbar. The button shows the current profile, such as
   **VibeStudio** or **TrenchBroom**.
2. Choose **Browse Editor Profiles…** to search by name, aliases such as NRC or TB, or engine family.
3. Review the default layout, gestures, command shortcuts and workflow differences, then choose **Use Profile**.
   Browsing does not change your settings. The **Controls like** menu still offers an immediate selection.

The same choice is **Editor profile** in **Settings** > **Appearance and Language** > **Editing**, and the
**Workspace and Editor Profile** step of [first-run setup](manual/first-run.md) opens it with **Choose Editor Profile**.

| Profile | ID | Starting views | Grid |
| --- | --- | --- | --- |
| VibeStudio Default | `vibestudio-default` | One view, 2D first | 64 units |
| TrenchBroom Style | `trenchbroom` | One view, 3D first | 16 units |
| NetRadiant Custom Style | `netradiant-custom` | 3D camera beside a 2D view | 16 units |
| NetRadiant Style | `netradiant` | 3D camera beside a 2D view | 8 units |
| GtkRadiant 1.6.0 Style | `gtkradiant-1-6` | 3D camera beside a 2D view | 8 units |
| GtkRadiant 1.4 Style | `gtkradiant-1-4` | 3D camera beside a 2D view | 8 units |
| GtkRadiant 1.5 Style | `gtkradiant-1-5` | 3D camera beside a 2D view | 8 units |
| QeRadiant Style | `qeradiant` | 3D camera beside a 2D view | 8 units |
| Q3Radiant Style | `q3radiant` | 3D camera beside a 2D view | 8 units |
| DoomEdit Style | `doomedit` | 3D camera beside a 2D view | 8 units |
| BSP Quake Editor Style | `bsp` | Four views: camera, top, front and side | 16 units |
| DarkRadiant Style | `darkradiant` | 3D camera beside a 2D view | 8 units |
| QuArK Style | `quark` | Four views: camera, top, front and side | 16 units |
| Hammer / Worldcraft Style | `hammer` | Four views: camera, top, front and side | 16 units |
| J.A.C.K. Style | `jack` | Four views: camera, top, front and side | 16 units |
| Sledge Style | `sledge` | Four views: camera, top, front and side | 16 units |
| Doom Builder 2 / X Style | `doom-builder` | One view, 2D first | 32 units |
| Ultimate Doom Builder Style | `ultimate-doom-builder` | One view, 2D first | 32 units |
| SLADE Style | `slade` | One view, 2D first | 32 units |
| Eureka Style | `eureka` | One view, 2D first | 16 units |
| Unreal Editor Style | `unreal` | One view, 3D first | 16 units |
| Unity Scene View Style | `unity` | One view, 3D first | 16 units |
| Godot 3D Style | `godot` | One view, 3D first | 16 units |
| Blender Style | `blender` | One view, 3D first | 16 units |

CLI profile selection also accepts familiar names such as `NRC`, `TB`, `GtkRadiant 1.4.0`,
`GtkRadiant 1.5.0`, `GtkRadiant 1.6.0` and `QE Radiant`. These resolve to the same saved
profile IDs. Quote names containing spaces, for example `editor select "GtkRadiant 1.5.0"`.

### See every gesture and key

Choose **Controls** > **Show Every Gesture and Key...** for a searchable list of everything the Levels views answer to
under your profile, including your own changes. The window can stay open beside your work. Where a profile differs
from the editor it follows, the list starts with **Profile adaptations**. Each Levels view also gives a screen reader
its controls after its own description, and **Help** > **Keyboard Shortcuts** lists the keys of every command.

The **Layout** button next to **Controls** can override the profile's arrangement with **One view, 2D first**,
**One view, 3D first**, **3D camera beside a 2D view**, **Four views: camera, top, front and side** or
**Camera Above Plans** (a wide camera above Top, Front and Side), or **Camera Beside Plans**
(a tall camera beside three stacked plans), keeping the
profile's gestures and keys. **Follow Editor Profile** goes back to the profile's own layout.

### Customise gestures and camera keys

1. Choose **Controls** > **Customize Gestures…**. The same window opens from **Customize Gestures…** under
   **Editor profile** in **Settings**, and from **View** > **Customize Editor Gestures…**. It edits the current profile
   only; each profile keeps its own changes.
2. Change fields on the **2D Plan**, **3D Camera**, **Camera Keys** and **Editing** tabs. **Profile default** follows the built-in
   value. Key fields also offer **Custom key**, where you press the key, and **None**, which turns the key off.
3. Read the status line under the tabs. A conflict, such as two gestures on one button, disables **Apply**. If a
   camera key might take priority over a command's key, a button such as **2 possible shortcut overlaps** lists them.
4. Choose **Apply**. Closing the window without applying drops your changes, and **Restore Defaults** sets every field
   back to the profile's own values, ready to apply.

Movement keys are single keys without <kbd>Ctrl</kbd>, <kbd>Alt</kbd> or <kbd>Shift</kbd>, and one key cannot move two
ways. <kbd>Esc</kbd>, <kbd>Tab</kbd>, modifier keys and lock keys are reserved, except that **Hold during mouse look to keep the
selection in view** may be <kbd>Tab</kbd>, since it acts only during mouse look. **Toggle mouse look** may use modifiers.
**Hold to pan** (2D Plan) and **Hold for mouse look** (Camera Keys) work while you hold the key, and stop when you
release it or the view loses focus.

**Export…** saves your changes to a VibeStudio `.json` file, and **Import…** loads one into the window for you to
apply. A file only imports into the profile it was made for.

The same duplicate preference is available to automation:

```sh
vibestudio --cli editor gestures netradiant-custom --set editing.nudgeAfterDuplicate=on --json
vibestudio --cli editor gestures netradiant-custom --set editing.nudgeAfterDuplicate=default --json
```

The **Editing** tab also has **Choosing or stepping the grid turns Snap on**, which is on in the NetRadiant Custom
and NetRadiant profiles and off elsewhere; `editing.gridChangeEnablesSnap=on|off` sets it from the command line.
**Quick turns pivot on the grid point nearest the selection's centre while Snap is on** is on in the NetRadiant Custom
and TrenchBroom profiles and off elsewhere, so **Rotate 90° Left/Right** and **Rotate X/Y/Z** keep on-grid brushes on the
grid there; `editing.quickTurnSnapsPivot=on|off` sets it from the command line.
**Flips mirror about the selection's centre rounded to half the grid while Snap is on** is on in the TrenchBroom
profile and off elsewhere, so an on-grid selection flips in place and an off-grid one keeps its on-grid sides on the
grid; `editing.flipSnapsPivot=on|off` sets it from the command line.

Add `--dry-run` to preview the preference without writing settings. This changes
ordinary and unique GUI duplication; `map duplicate` and **Duplicate with Offset…** use
their explicit offsets.

### Customise command keys

1. Choose **Help** > **Keyboard Shortcuts**, or **Keyboard Shortcuts…** in **Settings** > **Accessibility**.
2. Type part of a command name, a key or a page in the filter. The **Works on** column shows where each key acts.
3. Select the command, choose **Change Keys…** and press the new keys. The window tells you whether they are free, or
   which command they would be taken from.
4. Choose **Assign**. **No Keys** leaves the command without keys; it still runs from its menu and from command search.

- New keys need <kbd>Ctrl</kbd> or <kbd>Alt</kbd>, or a function key, because plain keys are typed in fields and used
  by the views.
- A few commands the studio relies on keep their keys, and you cannot take keys from them.
- **Reset** gives the selected command its default keys again, and **Reset All** does so for every command you changed.
- The list also shows keys the Levels views handle themselves, such as <kbd>F</kbd> to frame the selection. These
  cannot be changed; where a profile gives the same key to a command, the command wins.

### Behaviour every profile shares

- In component mode, **Alt+wheel** cycles overlapping handles; **Tab** and
  **Shift+Tab** cycle at the last component pick in the focused view. These local
  component keys take precedence over assigned commands while that mode is
  active. The named **Next/Previous Overlapping Component** commands can be
  assigned other keys. **Cycle overlapping components with wheel (None
  disables)** in **Customize Editor Gestures…** changes each view's wheel chord.
  An older custom FOV wheel binding keeps its chord and disables conflicting
  new camera cycling; explicitly assigning both to overlapping chords is refused.
- In a 2D view, a right click opens the map actions menu, a left drag on the selection moves it, and the handles
  resize it. In the 3D view, the labelled X, Y and Z handles resize a Quake-family selection.
- The **Draw Brush**, **Paint** and **Sample** camera tools work the same everywhere; while one is active it takes the
  plain left button in the 3D view. New brushes use the material in the **Material** box above the views.
- Every command stays in the menus and command search, whatever key a profile gives it. A command with no default key,
  such as **Cycle Map View** or **Zoom In**, gets one only from a profile or from you, and that key works only on the
  Levels page.
- The 3D view takes its fly keys only while it has focus; elsewhere those keys keep their commands.
- Losing focus, hiding a view or switching profile ends mouse look and camera flight. A selection dragged in the 3D
  view moves as one undo step, and <kbd>Esc</kbd> drops a move still in progress.
- Reduced motion does not slow mouse look or flight, which follow your hand.

### Move and insert components

**Insert Vertex** is available in command search and Geometry for every profile.
In the TrenchBroom profile, **Shift+drag** on an edge or face grid point also
starts insertion while editing vertices. Existing vertex handles retain their
selection role. Customise or disable this gesture with
`plan.componentInsertModifiers` and `camera.componentInsertModifiers` in
**Customize Editor Gestures…**; `none` disables the modifier gesture and leaves
the named tool available. Both fields use the existing profile import/export,
reset and CLI settings.

The TrenchBroom profile enables **World-axis component dragging with live
constraints and snapping** for movement and insertion. Perspective drags stay
horizontal; hold **Alt** for vertical movement. In plan views, movement stays
on the displayed plane and Alt has no effect. Press **Shift** after moving to
constrain the dominant movement axis. Release it to move freely again.

Snapping is relative to the grabbed handle. Hold **Ctrl** (**Cmd** on macOS)
for absolute vertex snapping; this can also align its hidden coordinate to
the grid. Edges and faces keep relative snapping. A Ctrl click still toggles
selection; dragging preserves the selected set when the grabbed geometry is
already selected. Ctrl and Alt may accompany the Shift insertion gesture.
Initial Shift starts insertion without choosing an axis.

`plan.componentAxisDrag` and `camera.componentAxisDrag` independently enable
this behaviour through gesture preferences, import/export and `editor gestures`.
Other profiles keep their existing defaults. Constraint and snap state appear
in the viewport description; numeric component tools remain available in every
profile. Labelled X/Y/Z segments show the movement distance, with a thicker
segment for the constrained axis and dashes while geometry is being checked.
Leaving vertical movement starts a fresh relative trace; the status retains
the total displacement. Complete TB/NRC gesture parity remains planned.

To snap exactly to another corner, pick one geometric brush vertex, then
**Alt+Shift+click** an existing vertex in the TrenchBroom profile. Coincident
picks in several brushes can move together. The target bypasses grid rounding;
merges, scene locks and component UV lock use the normal geometry checks and
one undo step. A drag with this chord moves the grabbed handle instead.

`plan.componentQuickSnapModifiers` and `camera.componentQuickSnapModifiers`
customise or disable this click independently of world-axis dragging. Their TB
default is `shift+alt`; other profiles use `none`. An older conflicting
selection shortcut keeps priority and disables the inherited quick-snap chord;
explicitly assigning both roles to one chord is refused. **Snap Picked Vertex…**
provides a keyboard target list in every profile. Projected plan stacks can
contain several different depths: use overlap cycling to pick one geometric
corner before snapping.

### Use the command line

The `editor` commands read and change the same settings as the studio. Profile IDs and aliases such as `hammer++`,
`udb` or `slade3` both work; settings always store the profile's ID. A change made on the command line applies the
next time the studio loads its settings; it does not control a studio that is already running.

```sh
vibestudio --cli editor profiles
vibestudio --cli editor controls trenchbroom
vibestudio --cli editor select netradiant-custom
vibestudio --cli editor gestures hammer --set camera.flyKeys.forward=I --dry-run --json
vibestudio --cli editor gestures trenchbroom --set plan.componentCycleWheelModifiers=ctrl+alt --set camera.componentCycleWheelModifiers=none
```

| Command | What it does |
| --- | --- |
| `editor profiles` | List every profile with its bindings |
| `editor current` | Show the selected profile |
| `editor select <profile>` | Select a profile |
| `editor controls [profile]` | Print a profile's layout, grid, 2D gestures, 3D camera and keys, with your changes |
| `editor gestures [profile]` | Show, change (`--set field=value`), reset (`--reset`), import (`--input`) or export (`--output`) gesture settings |
| `editor clipper` | Inspect or change shared clipper preferences, including `--set wrapMerge=on`, preview with `--dry-run`, or restore defaults with `--reset` |
| `editor layout [preference]` | Show or set the layout: `profile`, `single-2d`, `single-3d`, `camera-and-plan`, `four-views`, `camera-above-plans` or `camera-beside-plans`; `--panes front,side` (or `all`) chooses the plan panes the three-plan layouts show |
| `editor view-links` | Show or set the linked 2D view and camera-follow options |
| `editor keys` | List the keys you gave commands; `--reset` puts every default back |

Add `--json` for machine-readable output, and `--settings-file <path>` to work on a separate settings file instead of
your own. [Command line](manual/cli.md) covers the rest.

Clipper fields are `material=inherit|current|custom`, `customTexture=<name>`,
`resetPoints=on|off`, `resetSide=on|off`, `twoPointsInPlan=on|off` and
`doubleClick=none|clip|split`, plus `wrapMerge=on|off`. Repeat `--set` for an atomic change. **Quick Clip**
is available as a named command in every profile; NetRadiant Custom additionally
uses Ctrl+left-click in plan views. Its `plan.quickClipModifiers` gesture is
remappable, with `none` disabling it. Existing custom selection chords take
priority over this new default. See [clipping](manual/levels.md#clip-brushes-across-views)
for material, reset and cancellation behaviour.

Camera point behaviour is configured per profile with **Customize Gestures… >
3D Camera** or `editor gestures`: `camera.clipViewAxis=on|off` chooses the
two-point plane policy, and `camera.clipDragStyle=surface|world|view` chooses
modified point movement. NRC defaults to `on` and `world`; other profiles use
`off` and `surface`. Hold Shift or Alt after pressing a point for constrained
movement, or Ctrl to return to brush surfaces. These settings share normal
dry-run, import/export and atomic preference application.

### Profile reference

Each block lists the profile's mouse gestures and the keys that differ from the VibeStudio defaults, which are listed
in [Level editing](manual/levels.md#work-from-the-keyboard). Rows that every profile shares are described under
[Behaviour every profile shares](manual/editor-profiles.md#behaviour-every-profile-shares). On macOS, <kbd>Ctrl</kbd> means <kbd>Cmd</kbd>, for
keys and clicks alike.

<details>
<summary>VibeStudio Default</summary>

ID `vibestudio-default`. One view, 2D first, 64-unit grid. 3D camera: orthographic, orbiting the map.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Middle drag | Middle drag, <kbd>Shift</kbd>+left drag, <kbd>Ctrl</kbd>+left drag |
| Zoom | Wheel | Wheel |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click adds one | Left click |
| Select by area | Left drag over empty space | — |
| Move the selection | Arrow keys nudge it | — |
| Resize the selection | Left drag its handles | — |
| Orbit | — | Left drag |

| Command | Keys |
| --- | --- |
| Maximize or Restore Active View | <kbd>Ctrl</kbd>+<kbd>Space</kbd> |

Every other command keeps the keys listed in [Level editing](manual/levels.md#work-from-the-keyboard). Not implemented yet: Focus Level View (<kbd>F3</kbd>) and Focus Inspector (<kbd>F8</kbd>).

</details>

<details>
<summary>TrenchBroom Style</summary>

ID `trenchbroom`. One view, 3D first, 16-unit grid. 3D camera: first-person, 90 degree field of view.

**Sweep Tool** uses <kbd>Y</kbd> with brush faces picked. Its active viewport
uses Enter to create and `[` / `]` to decrease/increase scale, with user command
shortcuts taking precedence. See [Sweep selected faces](manual/levels.md#sweep-selected-faces)
for cap movement, rotation, scaling and cancellation.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Right drag, middle drag | Middle drag |
| Zoom | Wheel | <kbd>Shift</kbd>+wheel zooms the field of view |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click picks a face |
| Select by area | <kbd>Shift</kbd>+left drag | — |
| Draw a brush | Left drag over empty space (<kbd>Shift</kbd> square, <kbd>Ctrl</kbd>+<kbd>Shift</kbd> cube) | — |
| Move the selection | Arrow keys nudge it; <kbd>Page Up</kbd> and <kbd>Page Down</kbd> along the view's depth | Arrow keys nudge it along the nearest horizontal axes, <kbd>Page Up</kbd> and <kbd>Page Down</kbd> along Z; left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles | — |
| Look around | — | Right drag |
| Orbit | — | <kbd>Alt</kbd>+right drag |
| Move forward and back | — | Wheel |
| Fly | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right, <kbd>Q</kbd> up, <kbd>X</kbd> down; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |

| Command | Keys |
| --- | --- |
| Delete Selection | <kbd>Delete</kbd>, <kbd>Backspace</kbd> |
| Invert Selection | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>A</kbd> |
| Isolate Selection | <kbd>Ctrl</kbd>+<kbd>I</kbd> |
| Hide Selection / Show All Hidden | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>I</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd> |
| Clip Tool | <kbd>C</kbd> |
| Assemble Brush Tool | <kbd>B</kbd>; <kbd>Enter</kbd> in a view creates the brush |
| Smaller Grid / Larger Grid | <kbd>-</kbd>, <kbd>[</kbd> / <kbd>+</kbd>, <kbd>=</kbd>, <kbd>]</kbd> |
| Snap to Grid | <kbd>Alt</kbd>+<kbd>0</kbd> |
| Cycle Map View | <kbd>Space</kbd> |
| Frame Selection | <kbd>Ctrl</kbd>+<kbd>U</kbd> |
| Reset Camera Zoom | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Z</kbd> |
| Carve | <kbd>Ctrl</kbd>+<kbd>K</kbd> |
| Hollow | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>K</kbd> |
| Show Grid | <kbd>0</kbd> |
| Grid 1 to 256 | <kbd>1</kbd> to <kbd>9</kbd> |
| Edit Vertices / Edit Edges / Edit Faces | <kbd>V</kbd> / <kbd>E</kbd> / <kbd>F</kbd> |
| Snap Vertices to Grid | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>V</kbd> |
| Create Linked Duplicate | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>D</kbd> |
| Lock Component UVs | <kbd>U</kbd> |
| Roll Clockwise / Counter-clockwise | <kbd>Alt</kbd>+<kbd>Up</kbd> / <kbd>Alt</kbd>+<kbd>Down</kbd> |
| Rotate 90° Right / Left (yaw) | <kbd>Alt</kbd>+<kbd>Left</kbd> / <kbd>Alt</kbd>+<kbd>Right</kbd> |
| Pitch Clockwise / Counter-clockwise | <kbd>Alt</kbd>+<kbd>Page Up</kbd> / <kbd>Alt</kbd>+<kbd>Page Down</kbd> |
| Flip Horizontal / Flip Vertical | <kbd>Ctrl</kbd>+<kbd>F</kbd> / <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F</kbd> |
| Duplicate and Move Left / Right / Forward / Back | <kbd>Ctrl</kbd>+<kbd>Left</kbd> / <kbd>Ctrl</kbd>+<kbd>Right</kbd> / <kbd>Ctrl</kbd>+<kbd>Up</kbd> / <kbd>Ctrl</kbd>+<kbd>Down</kbd> (with nothing to copy, they pan the 2D view) |
| Duplicate and Move Depth Up / Depth Down | <kbd>Ctrl</kbd>+<kbd>Page Up</kbd> / <kbd>Ctrl</kbd>+<kbd>Page Down</kbd> (Doom maps keep them for the ceilings) |

**3D Wireframe**, **Draw Sector** and **Grid 0.125** to **Grid 0.5** have no key, as in TrenchBroom;
<kbd>-</kbd> and <kbd>[</kbd> reach the fractional grids. <kbd>V</kbd>, <kbd>E</kbd> and
<kbd>F</kbd> turn on vertex, edge and face editing, as TrenchBroom's tools do;
pressing the key of the current mode turns it off. <kbd>F</kbd> no longer frames
the selection in the 2D views here: use <kbd>Ctrl</kbd>+<kbd>U</kbd>. Patch control
points share vertex editing rather than having their own tool. Object area selection
uses <kbd>Shift</kbd>+drag in the plan view. In component mode, a plain lasso
toggles enclosed handles, while <kbd>Ctrl</kbd> adds them, in both plan and camera.
The component lasso policy is configurable in **Customize Editor Gestures…**.
All profiles share complete-boundary conversion when switching component modes,
plus touching conversion, Grow, Shrink and Connected commands under
**Component Selection**. These are VibeStudio's common topology-selection rules;
they do not establish native shortcut parity with another editor.
For automation, use `editor gestures trenchbroom --set plan.componentBandToggles=off --set camera.componentBandToggles=off`;
`--reset` restores this profile's plain-toggle/Ctrl-add behaviour.
<kbd>Ctrl</kbd>+<kbd>F</kbd> stays **Find** instead of flipping objects.

</details>

<details>
<summary>NetRadiant Custom Style</summary>

ID `netradiant-custom`. 3D camera beside a 2D view, 16-unit grid. 3D camera: first-person, 100 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Right drag | Right drag |
| Zoom | Wheel, <kbd>Alt</kbd>+right drag | — |
| Select | Left click; again to pick the next object under the pointer; <kbd>Shift</kbd>+left click adds or removes one | Left click; <kbd>Shift</kbd>+left click adds or removes one; <kbd>Ctrl</kbd>+left click picks a face |
| Select by area | <kbd>Shift</kbd>+left drag | — |
| Draw a brush | Left drag over empty space, nothing selected (<kbd>Shift</kbd> square, <kbd>Ctrl</kbd> cube) | — |
| Move the selection | <kbd>Alt</kbd>+arrow keys nudge it; keypad <kbd>+</kbd> and <kbd>-</kbd> along Z | <kbd>Alt</kbd>+arrow keys nudge it in the last 2D view's axes, keypad <kbd>+</kbd> and <kbd>-</kbd> along Z; left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles, or over empty space beside it | — |
| Steer the 3D camera | Middle click aims it at the point; <kbd>Ctrl</kbd>+middle click moves it there; arrow keys drive it | — |
| Look around | — | Right click to start or stop, then move the mouse |
| Orbit | — | <kbd>Alt</kbd>+right drag |
| Move forward and back | — | Wheel, toward the pointer |
| Fly while looking | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |
| Keep the selection in view | — | Hold <kbd>Tab</kbd> while looking; the wheel moves nearer or farther |
| Drive | — | <kbd>Up</kbd> forward, <kbd>Down</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right, <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right; while looking, the turn keys strafe |
| Material and surface clicks | — | Middle click: sample the material and copy the surface; <kbd>Ctrl</kbd>+middle click: wrap the copied surface onto one face; <kbd>Shift</kbd>+middle click: paste values onto the hit and the selection; <kbd>Shift</kbd>+<kbd>Alt</kbd>+middle click: paste mapping values only; <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+middle click: wrap mapping only; <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+middle click: project the copied surface onto the hit and the selection; <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Alt</kbd>+middle click: project mapping only |

| Command | Keys |
| --- | --- |
| Duplicate Selection | <kbd>Space</kbd> |
| Duplicate, Make Unique | <kbd>Shift</kbd>+<kbd>Space</kbd> |
| Delete Selection | <kbd>Delete</kbd>, <kbd>Backspace</kbd>, <kbd>Z</kbd> |
| Select None | <kbd>C</kbd> |
| Invert Selection | <kbd>I</kbd> |
| Next 2D View | <kbd>Ctrl</kbd>+<kbd>Tab</kbd> |
| Top View / Front View / Side View | <kbd>Keypad 7</kbd> / <kbd>Keypad 1</kbd> / <kbd>Keypad 3</kbd> |
| 3D View | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> |
| Maximize or Restore Active View | <kbd>F12</kbd> |
| Frame Selection | <kbd>&#96;</kbd> |
| Level Camera | <kbd>End</kbd> |
| Next Camera Mode / Previous Camera Mode | <kbd>Shift</kbd>+<kbd>]</kbd> / <kbd>Shift</kbd>+<kbd>[</kbd> |
| Carve | <kbd>Shift</kbd>+<kbd>U</kbd> |
| Round Selection to Grid | <kbd>Ctrl</kbd>+<kbd>G</kbd> |
| Repeat Transforms | <kbd>Ctrl</kbd>+<kbd>R</kbd> |
| Rotate Selection… / Scale Selection… | <kbd>Shift</kbd>+<kbd>R</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Make 3-Sided Prism to Make 9-Sided Prism (in a map view, with a brush selected; otherwise the mode keys) | <kbd>Ctrl</kbd>+<kbd>3</kbd> to <kbd>Ctrl</kbd>+<kbd>9</kbd> |
| Move to Camera | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>V</kbd> |
| Connect Entities | <kbd>Ctrl</kbd>+<kbd>K</kbd> |
| KillConnect Entities (Quake, Nexuiz or Xonotic context) | <kbd>Shift</kbd>+<kbd>K</kbd> |
| Show Grid | <kbd>0</kbd> |
| Grid 1 to 256 | <kbd>1</kbd> to <kbd>9</kbd> |
| Edit Vertices / Edit Edges / Edit Faces | <kbd>V</kbd> / <kbd>E</kbd> / <kbd>F</kbd> |
| Centre Views on Selection | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> |
| Insert Patch Rows / Insert Patch Columns | <kbd>Ctrl</kbd>+<kbd>Keypad +</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Keypad +</kbd> |
| Delete Last Patch Rows / Delete Last Patch Columns | <kbd>Ctrl</kbd>+<kbd>Keypad -</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Keypad -</kbd> |
| Invert Patch Facing | <kbd>Ctrl</kbd>+<kbd>I</kbd> |
| Transpose Patch | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>M</kbd> |
| Thicken Patch | <kbd>Ctrl</kbd>+<kbd>T</kbd> |
| Smooth Patch Columns | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>W</kbd> |
| Flip Texture V | <kbd>Shift</kbd>+<kbd>I</kbd> |
| Rotate Texture + / Rotate Texture − (45° about the texture origin) | <kbd>Shift</kbd>+<kbd>Page Up</kbd> / <kbd>Shift</kbd>+<kbd>Page Down</kbd> |
| Texture Scale V + / V − / U − / U + (Radiant scale step) | <kbd>Ctrl</kbd>+<kbd>Up</kbd> / <kbd>Ctrl</kbd>+<kbd>Down</kbd> / <kbd>Ctrl</kbd>+<kbd>Left</kbd> / <kbd>Ctrl</kbd>+<kbd>Right</kbd> |
| Texture Lock | <kbd>Shift</kbd>+<kbd>T</kbd> |
| Show Surfaces (shows the **Surface** tab, or hides it again; in mouse look <kbd>S</kbd> moves the camera back) | <kbd>S</kbd> |
| Make Detail / Make Structural | <kbd>Alt</kbd>+<kbd>D</kbd> / <kbd>Alt</kbd>+<kbd>S</kbd> |
| Move to Layer menu / Expand Selection to Layers | <kbd>Shift</kbd>+<kbd>Q</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Q</kbd> (NetRadiant Custom's <kbd>Ctrl</kbd>+<kbd>Q</kbd> stays **Quit**) |

**3D Wireframe** has no key. <kbd>V</kbd>, <kbd>E</kbd> and <kbd>F</kbd> turn vertex, edge and face editing on, or off
again in the mode already on, so <kbd>F</kbd> no longer frames the selection in the 2D views here; use <kbd>&#96;</kbd>.
Flip Texture U (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd>) and Smooth Patch Rows (<kbd>Ctrl</kbd>+<kbd>W</kbd>) have no key,
because the studio keeps those for **Initialize Project Manifest** and **Close Package**; give them keys in **Help** >
**Keyboard Shortcuts** if you want them. The texture keys step the **Surfaces** target as NetRadiant Custom's Surface
Inspector does: the **Surfaces** tab starts at a 45° **Rotation step** about the **Texture origin** and a 0.5 **Scale step**,
and <kbd>Ctrl</kbd>+arrow keys pan the 2D view only while nothing is targeted; see
[Step textures by Radiant increments](manual/levels.md#step-textures-by-radiant-increments). **Grid 0.125** to **Grid 0.5**, **Grid 512** and **Grid 1024** have no key, as in NetRadiant Custom;
<kbd>[</kbd> and <kbd>]</kbd> reach them. Choosing or stepping
the grid turns **Snap** on, as NetRadiant Custom does; you can turn that off on the **Editing** tab of
**Customize Gestures…**. Not implemented yet: Fit Texture (<kbd>Ctrl</kbd>+<kbd>F</kbd>) and Expand Selection (<kbd>Shift</kbd>+<kbd>E</kbd>). Copying or wrapping from a patch, and sampling depth or light colour, are not supported.

</details>

<details>
<summary>NetRadiant Style</summary>

ID `netradiant`, also `net-radiant`, `xonotic-netradiant`, `netradiant-classic`. 3D camera beside a 2D view, 8-unit grid. 3D camera: first-person, 110 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Right drag | — |
| Zoom | Wheel, <kbd>Alt</kbd>+right drag | — |
| Select | Left click; again to pick the next object under the pointer; <kbd>Shift</kbd>+left click adds or removes one | Left click; <kbd>Shift</kbd>+left click adds or removes one; <kbd>Ctrl</kbd>+left click picks a face |
| Select by area | <kbd>Shift</kbd>+left drag | — |
| Draw a brush | Left drag over empty space, nothing selected (<kbd>Shift</kbd> square, <kbd>Ctrl</kbd> cube) | — |
| Move the selection | <kbd>Alt</kbd>+arrow keys nudge it; keypad <kbd>+</kbd> and <kbd>-</kbd> along Z | <kbd>Alt</kbd>+arrow keys nudge it in the last 2D view's axes, keypad <kbd>+</kbd> and <kbd>-</kbd> along Z; left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles, or over empty space beside it | — |
| Steer the 3D camera | Middle click aims it at the point; <kbd>Ctrl</kbd>+middle click moves it there; arrow keys drive it | — |
| Look around | — | Right click to start or stop, then move the mouse |
| Move forward and back | — | Wheel, toward the pointer |
| Fly while looking | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |
| Keep the selection in view | — | Hold <kbd>Tab</kbd> while looking; the wheel moves nearer or farther |
| Drive | — | <kbd>Up</kbd> forward, <kbd>Down</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right, <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right; while looking, the turn keys strafe |
| Material and surface clicks | — | Middle click: sample the material and copy the surface; <kbd>Shift</kbd>+middle click: paste the copied surface on one face |

| Command | Keys |
| --- | --- |
| Duplicate Selection | <kbd>Space</kbd> |
| Delete Selection | <kbd>Backspace</kbd>, <kbd>Z</kbd> |
| Select None | <kbd>Esc</kbd>, <kbd>C</kbd> |
| Invert Selection | <kbd>I</kbd> |
| Next 2D View | <kbd>Ctrl</kbd>+<kbd>Tab</kbd> |
| Top View / Front View / Side View | <kbd>Keypad 7</kbd> / <kbd>Keypad 1</kbd> / <kbd>Keypad 3</kbd> |
| 3D View | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> |
| Maximize or Restore Active View | <kbd>F12</kbd> |
| Frame Selection | <kbd>&#96;</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> |
| Level Camera | <kbd>End</kbd> |
| Carve | <kbd>Shift</kbd>+<kbd>U</kbd> |
| Snap Selection to Grid | <kbd>Ctrl</kbd>+<kbd>G</kbd> |
| Connect Entities | <kbd>Ctrl</kbd>+<kbd>K</kbd> |
| Redo Map Edit | <kbd>Ctrl</kbd>+<kbd>Y</kbd> |
| Show Grid | <kbd>0</kbd> |
| Select All of This Class | <kbd>Shift</kbd>+<kbd>A</kbd> |
| Zoom In / Zoom Out | <kbd>Delete</kbd> / <kbd>Insert</kbd> |
| Show Surfaces (shows the **Surface** tab, or hides it again; in mouse look <kbd>S</kbd> moves the camera back) | <kbd>S</kbd> |
| Grid 1 to 256 | <kbd>1</kbd> to <kbd>9</kbd> |

**Select All** and **3D Wireframe** have no key. The NetRadiant Custom keys for vertex, edge and face editing, the
patch commands, **Make Detail**, **Make Structural**, **Texture Lock**, **Centre Views on Selection** and the texture
rotate and scale steps are not given here, because they were checked against NetRadiant Custom only; the **Surfaces**
tab keeps its 15° **Rotation step** about each **Face centre**. As in NetRadiant, choosing or stepping the grid turns
**Snap** on. Outside mouse look, Tab keeps moving keyboard focus; held during mouse look, it keeps the selection in view, as NetRadiant's free focus does. Discrete camera steps, floor stepping, right-button selection painting and unique-target cloning are not reproduced. The upstream <kbd>Ctrl</kbd>+middle and <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+middle paste chords are unbound; choose one in **Customize Gestures…** if you want them.

</details>

<details>
<summary>GtkRadiant 1.6.0 Style</summary>

ID `gtkradiant-1-6`. 3D camera beside a 2D view, 8-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Right drag | — |
| Zoom | Wheel, <kbd>Shift</kbd>+right drag | — |
| Select | <kbd>Shift</kbd>+left click selects or deselects; a plain click selects nothing; <kbd>Shift</kbd>+<kbd>Alt</kbd>+left click picks the next object under the pointer | <kbd>Shift</kbd>+left click selects or deselects; a plain click selects nothing; <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+left click picks a face |
| Select by area | <kbd>Alt</kbd>+left drag | — |
| Draw a brush | Left drag over empty space, nothing selected | — |
| Move the selection | <kbd>Alt</kbd>+arrow keys nudge it; keypad <kbd>+</kbd> and <kbd>-</kbd> along Z | <kbd>Alt</kbd>+arrow keys nudge it in the last 2D view's axes, keypad <kbd>+</kbd> and <kbd>-</kbd> along Z; left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles, or over empty space beside it | — |
| Steer the 3D camera | Middle click aims it at the point; <kbd>Ctrl</kbd>+middle click moves it there; arrow keys drive it | — |
| Look around | — | Right click to start or stop, then move the mouse |
| Move forward and back | — | Wheel |
| Fly while looking | — | <kbd>Up</kbd> forward, <kbd>Down</kbd> back, <kbd>,</kbd> left, <kbd>.</kbd> right, <kbd>D</kbd> up, <kbd>C</kbd> down, <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |
| Drive | — | <kbd>Up</kbd> forward, <kbd>Down</kbd> back, <kbd>,</kbd> left, <kbd>.</kbd> right, <kbd>D</kbd> up, <kbd>C</kbd> down, <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right; while looking, the turn keys strafe |
| Material and surface clicks | — | Middle click: sample the material and copy the surface; <kbd>Shift</kbd>+middle click: paint the material on one surface; <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+middle click: paste the copied surface on one face; <kbd>Ctrl</kbd>+middle click: paste it on the whole brush |

| Command | Keys |
| --- | --- |
| Duplicate Selection | <kbd>Space</kbd> |
| Delete Selection | <kbd>Backspace</kbd> |
| Zoom In / Zoom Out | <kbd>Delete</kbd> / <kbd>Insert</kbd> |
| Select All of This Class | <kbd>Shift</kbd>+<kbd>A</kbd> |
| Invert Selection | <kbd>I</kbd> |
| Next 2D View | <kbd>Ctrl</kbd>+<kbd>Tab</kbd> |
| Frame Selection | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> |
| Level Camera | <kbd>End</kbd> |
| 3D View | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> |
| Carve | <kbd>Shift</kbd>+<kbd>U</kbd> |
| Snap Selection to Grid | <kbd>Ctrl</kbd>+<kbd>G</kbd> |
| Connect Entities | <kbd>Ctrl</kbd>+<kbd>K</kbd> |
| Load Leak Trail | <kbd>Shift</kbd>+<kbd>L</kbd> |
| Redo Map Edit | <kbd>Ctrl</kbd>+<kbd>Y</kbd> |
| Show Grid | <kbd>0</kbd> |
| Grid 1 to 256 | <kbd>1</kbd> to <kbd>9</kbd> |

**3D Wireframe** has no key. Not implemented yet: Drag Edges (<kbd>E</kbd>), Drag Vertices (<kbd>V</kbd>), Mouse Rotate (<kbd>R</kbd>) and Make Detail (<kbd>Ctrl</kbd>+<kbd>M</kbd>). The camera's <kbd>A</kbd> and <kbd>Z</kbd> pitch keys are not bound. Pasting a surface onto every selected object, and sampling brush depth or light colour, are not supported.

</details>

<details>
<summary>GtkRadiant 1.4 and 1.5 Styles</summary>

IDs `gtkradiant-1-4` and `gtkradiant-1-5`, also `gtk14` and `gtk15`. Both start with a
90-degree camera beside a plan and an 8-unit grid. They follow the 1.4.0-era ZeroRadiant
and GtkRadiant 1.5 sources; the original editor's complete toolset is not reproduced.

| Action | GtkRadiant 1.4 | GtkRadiant 1.5 |
| --- | --- | --- |
| Select | <kbd>Shift</kbd>+left click; <kbd>Shift</kbd>+<kbd>Alt</kbd> cycles stacked objects | Same |
| Select by area | <kbd>Alt</kbd>+left drag | <kbd>Shift</kbd>+left drag |
| Plan pan / zoom | Right drag / <kbd>Shift</kbd>+right drag or wheel | Same |
| Camera look | Right click starts/stops free look | Same |
| Camera steps outside free look | Arrows move/turn, comma/period strafe, <kbd>D</kbd>/<kbd>C</kbd> rise/sink, <kbd>A</kbd>/<kbd>Z</kbd> pitch up/down | Same |
| Camera flight during free look | Arrows move/strafe; comma/period, <kbd>D</kbd>/<kbd>C</kbd> also move | Arrow-key translation |
| Sample a surface | Middle click | Middle click |
| Paint material only | <kbd>Shift</kbd>+middle click | Unassigned |
| Paste surface on a brush / face | <kbd>Ctrl</kbd>+middle / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+middle | Face paste only: <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+middle |

Camera steps outside free look move 32 units or turn 22.5 degrees. Modified arrows remain
available to texture commands there. Both profiles clone with <kbd>Space</kbd>, delete with
<kbd>Backspace</kbd>, zoom the plan with <kbd>Delete</kbd>/<kbd>Insert</kbd>, merge brushes with
<kbd>Ctrl</kbd>+<kbd>U</kbd>, fit textures with <kbd>Shift</kbd>+<kbd>B</kbd>, frame selection with
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> and level the camera with <kbd>End</kbd>. <kbd>S</kbd> opens Surface Alignment,
<kbd>Shift</kbd>+<kbd>S</kbd> edits patches, <kbd>Shift</kbd>+<kbd>C</kbd> caps patches, and
<kbd>Shift</kbd>+<kbd>T</kbd> toggles texture lock. Shift+arrows shift texture U/V;
Shift+Page Up/Down rotate using the current Surfaces target and step settings.

1.5 area selection adds members instead of toggling them, and surface paste uses the hit face.
Native replacement-area selection, component manipulators, floor stepping,
Z-checker and preference imports are not reproduced. The 1.4 preset keeps <kbd>Shift</kbd>+<kbd>L</kbd>
for Load Leak Trail; 1.5 leaves it unassigned. Neither preset adds Doom 3 map support.

</details>

<details>
<summary>QeRadiant Style</summary>

ID `qeradiant`, also `qe-radiant`, `qer`, `qe` or `quake2-radiant`. The classic Quake II
profile uses the camera, plan, Shift selection, Space cloning and Backspace deletion described
under Q3Radiant below. Right drag steers the camera; <kbd>Ctrl</kbd>+right drag pans it.
Texture fitting uses <kbd>Shift</kbd>+<kbd>5</kbd> or <kbd>Ctrl</kbd>+<kbd>F</kbd> while a map view
has focus, using the Surfaces target and current steps.

Q3-specific patch, hide/show, select-similar and texture-lock keys are unassigned, and so is
<kbd>End</kbd> for **Level Camera**, which the QeRadiant manual does not confirm. Those
VibeStudio commands remain available in menus and command search. Whole-entity selection mode,
animated entity previews, Alt+right texture dragging and Z-checker are not reproduced.
Middle-button sampling and surface paste follow the shared studio clipboard rules; sampling
does not automatically repaint the selected objects.

</details>

<details>
<summary>Q3Radiant Style</summary>

ID `q3radiant`, also `q3-radiant`, `quake3-radiant`, `quake-iii-radiant`. 3D camera beside a 2D view, 8-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Right drag | <kbd>Ctrl</kbd>+right drag |
| Zoom | Wheel | — |
| Select | <kbd>Shift</kbd>+left click selects or deselects; a plain click selects nothing; <kbd>Shift</kbd>+<kbd>Alt</kbd>+left click picks the next object under the pointer | <kbd>Shift</kbd>+left click selects or deselects; a plain click selects nothing; <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+left click picks a face |
| Select by area | <kbd>Alt</kbd>+left drag | — |
| Draw a brush | Left drag over empty space, nothing selected | — |
| Move the selection | <kbd>Alt</kbd>+arrow keys nudge it; keypad <kbd>+</kbd> and <kbd>-</kbd> along Z | <kbd>Alt</kbd>+arrow keys nudge it in the last 2D view's axes, keypad <kbd>+</kbd> and <kbd>-</kbd> along Z; left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles, or over empty space beside it | — |
| Steer the 3D camera | Middle click aims it at the point; <kbd>Ctrl</kbd>+middle click moves it there; arrow keys drive it in fixed steps of 32 units or 22.5 degrees | — |
| Steer | — | Hold the right button: above or below the centre moves forward or back, left or right turns; the centre stops, and releasing or <kbd>Esc</kbd> ends it |
| Move forward and back | — | Wheel |
| Drive | — | <kbd>Up</kbd> forward, <kbd>Down</kbd> back, <kbd>,</kbd> left, <kbd>.</kbd> right, <kbd>D</kbd> up, <kbd>C</kbd> down, <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right, <kbd>A</kbd> look up, <kbd>Z</kbd> look down; each press steps 32 units or 22.5 degrees on the ground plane |
| Material and surface clicks | — | Middle click: sample the material and copy the surface; <kbd>Shift</kbd>+middle click: paint the material on one surface; <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+middle click: paste the copied surface on one face; <kbd>Ctrl</kbd>+middle click: paste it on the whole brush |

| Command | Keys |
| --- | --- |
| Duplicate Selection | <kbd>Space</kbd> |
| Delete Selection | <kbd>Backspace</kbd> |
| Zoom In / Zoom Out | <kbd>Delete</kbd> / <kbd>Insert</kbd> |
| Select All of This Class | <kbd>Shift</kbd>+<kbd>A</kbd> |
| Next 2D View | <kbd>Ctrl</kbd>+<kbd>Tab</kbd> |
| Level Camera | <kbd>End</kbd> |
| 3D View | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> |
| Carve | <kbd>Shift</kbd>+<kbd>U</kbd> |
| Snap Selection to Grid | <kbd>Ctrl</kbd>+<kbd>G</kbd> |
| Connect Entities | <kbd>Ctrl</kbd>+<kbd>K</kbd> |
| Redo Map Edit | <kbd>Ctrl</kbd>+<kbd>Y</kbd> |
| Show Grid | <kbd>0</kbd> |
| Select None | <kbd>Esc</kbd> |
| Merge Brushes | <kbd>Ctrl</kbd>+<kbd>U</kbd> |
| Surface Alignment | <kbd>S</kbd> |
| Edit Patch Control Points | <kbd>Shift</kbd>+<kbd>S</kbd> |
| Cap Patch | <kbd>Shift</kbd>+<kbd>C</kbd> |
| Texture Lock | <kbd>Shift</kbd>+<kbd>T</kbd> |
| Shift Texture U − / Shift Texture U + | <kbd>Shift</kbd>+<kbd>Left</kbd> / <kbd>Shift</kbd>+<kbd>Right</kbd> |
| Shift Texture V − / Shift Texture V + | <kbd>Shift</kbd>+<kbd>Down</kbd> / <kbd>Shift</kbd>+<kbd>Up</kbd> |
| Rotate Texture − / Rotate Texture + | <kbd>Shift</kbd>+<kbd>Page Up</kbd> / <kbd>Shift</kbd>+<kbd>Page Down</kbd> |
| Fit Texture 1 × 1 | <kbd>Shift</kbd>+<kbd>5</kbd> |
| Grid 1 to 256 | <kbd>1</kbd> to <kbd>9</kbd> |

**Invert Selection**, **Frame Selection**, **Load Leak Trail**, **3D Wireframe** and **Select All** have no key. Texture shifts use the step values on the **Surfaces** tab, not Q3Radiant's camera-relative shifts, and <kbd>Ctrl</kbd>+arrow scaling is unbound. Native preferences, the Z-checker, floor stepping, terrain, and bend and rotation modes are not reproduced.

</details>

<details>
<summary>DarkRadiant Style</summary>

ID `darkradiant`, also `dark-radiant`. 3D camera beside a 2D view, 8-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Right drag | — |
| Zoom | Wheel, <kbd>Shift</kbd>+right drag | — |
| Select | <kbd>Shift</kbd>+left click selects or deselects; a plain click selects nothing; <kbd>Shift</kbd>+<kbd>Alt</kbd>+left click picks the next object under the pointer | <kbd>Shift</kbd>+left click selects or deselects; a plain click selects nothing; <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+left click picks a face |
| Select by area | <kbd>Alt</kbd>+left drag | — |
| Draw a brush | Left drag over empty space, nothing selected | — |
| Move the selection | <kbd>Alt</kbd>+arrow keys nudge it; keypad <kbd>+</kbd> and <kbd>-</kbd> along Z | <kbd>Alt</kbd>+arrow keys nudge it in the last 2D view's axes, keypad <kbd>+</kbd> and <kbd>-</kbd> along Z; left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles, or over empty space beside it | — |
| Steer the 3D camera | Middle click aims it at the point; <kbd>Ctrl</kbd>+middle click moves it there; arrow keys drive it | — |
| Look around | — | Right click to start or stop, then move the mouse |
| Move forward and back | — | Wheel |
| Fly while looking | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |
| Drive | — | <kbd>Up</kbd> forward, <kbd>Down</kbd> back, <kbd>,</kbd> left, <kbd>.</kbd> right, <kbd>D</kbd> up, <kbd>C</kbd> down, <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right; while looking, the turn keys strafe |

| Command | Keys |
| --- | --- |
| Duplicate Selection | <kbd>Space</kbd> |
| Delete Selection | <kbd>Backspace</kbd> |
| Zoom In / Zoom Out | <kbd>Delete</kbd> / <kbd>Insert</kbd> |
| Select All of This Class | <kbd>Shift</kbd>+<kbd>A</kbd> |
| Invert Selection | <kbd>I</kbd> |
| Next 2D View | <kbd>Ctrl</kbd>+<kbd>Tab</kbd> |
| Frame Selection | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> |
| 3D View | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> |
| Carve | <kbd>Shift</kbd>+<kbd>U</kbd> |
| Snap Selection to Grid | <kbd>Ctrl</kbd>+<kbd>G</kbd> |
| Connect Entities | <kbd>Ctrl</kbd>+<kbd>K</kbd> |
| Load Leak Trail | <kbd>Shift</kbd>+<kbd>L</kbd> |
| Redo Map Edit | <kbd>Ctrl</kbd>+<kbd>Y</kbd> |
| Show Grid | <kbd>0</kbd> |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Grid 1 to 256 | <kbd>1</kbd> to <kbd>9</kbd> |

**3D Wireframe** has no key. Choosing it does not add Doom 3 or Quake 4 formats or a Dark Mod game connection.

</details>

<details>
<summary>QuArK Style</summary>

ID `quark`, also `quake-army-knife`. Four views: camera, top, front and side, 16-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Right drag | Middle drag |
| Zoom | Wheel, middle drag | — |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click adds one | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click picks a face |
| Select by area | Left drag over empty space | — |
| Move the selection | Arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles | — |
| Look around | — | Right drag |
| Move forward and back | — | Wheel |
| Drive | — | <kbd>Up</kbd> forward, <kbd>Down</kbd> back, <kbd>End</kbd> left, <kbd>Page Down</kbd> right, <kbd>D</kbd> up, <kbd>C</kbd> down, <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right; while looking, the turn keys strafe |

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Select None | <kbd>Esc</kbd> |

**3D Wireframe** and **Draw Sector** have no key. Not implemented yet: Object Properties (<kbd>Alt</kbd>+<kbd>Enter</kbd>), Focus Object Tree (<kbd>F6</kbd>) and Rename Object (<kbd>F2</kbd>). Four synchronised views replace QuArK's window layout. Its extra selection keys, tree hotkeys, multi-button gestures and project files are not reproduced.

</details>

<details>
<summary>Hammer / Worldcraft Style</summary>

ID `hammer`, also `worldcraft`, `valve-hammer`, `hammer++`, `hammer-plus-plus`. Four views: camera, top, front and side, 16-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Middle drag | Right drag |
| Zoom | Wheel | — |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click adds one | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click picks a face |
| Select by area | Left drag over empty space | — |
| Move the selection | Arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles | — |
| Look around | — | Middle drag; <kbd>Z</kbd> turns mouse look on or off |
| Move forward and back | — | Wheel |
| Fly while looking | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Select None | <kbd>Shift</kbd>+<kbd>Q</kbd> |
| Clip Tool | <kbd>Shift</kbd>+<kbd>X</kbd> |
| Carve | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> |
| Hollow | <kbd>Ctrl</kbd>+<kbd>H</kbd> |
| Snap Selection to Grid | <kbd>Ctrl</kbd>+<kbd>B</kbd> |
| Flip Vertical | <kbd>Ctrl</kbd>+<kbd>I</kbd> |
| Flip Horizontal | <kbd>Ctrl</kbd>+<kbd>L</kbd> |
| Texture Lock | <kbd>Shift</kbd>+<kbd>L</kbd> |
| Frame Selection | <kbd>Ctrl</kbd>+<kbd>E</kbd> |
| Show Grid | <kbd>Shift</kbd>+<kbd>R</kbd> |
| Snap to Grid | <kbd>Shift</kbd>+<kbd>W</kbd> |
| Next 2D View | <kbd>Tab</kbd> |
| Maximize or Restore Active View | <kbd>Shift</kbd>+<kbd>Z</kbd> |
| Build and Launch | <kbd>F9</kbd> |
| Equalize View Sizes | <kbd>Ctrl</kbd>+<kbd>A</kbd> |

**Duplicate Selection**, **Select All**, **3D Wireframe** and **Draw Sector** have no key. Use **Edit** > **Duplicate Selection** and **Edit** > **Select All** instead. Space-drag, duplicate-on-drag, displacements, Source 2 modes and VMF or RMF files are not supported.

</details>

<details>
<summary>J.A.C.K. Style</summary>

ID `jack`, also `j.a.c.k.`, `jackhammer`. Four views: camera, top, front and side, 16-unit grid. 3D camera: first-person, 90 degree field of view.

Mouse gestures and camera keys are the same as **Hammer / Worldcraft Style**.

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Select None | <kbd>Ctrl</kbd>+<kbd>Q</kbd>, <kbd>Shift</kbd>+<kbd>Q</kbd> |
| Clip Tool | <kbd>Shift</kbd>+<kbd>X</kbd> |
| Carve | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> |
| Hollow | <kbd>Ctrl</kbd>+<kbd>U</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>H</kbd> |
| Snap Selection to Grid | <kbd>Ctrl</kbd>+<kbd>B</kbd> |
| Flip Vertical | <kbd>Ctrl</kbd>+<kbd>I</kbd> |
| Flip Horizontal | <kbd>Ctrl</kbd>+<kbd>L</kbd> |
| Texture Lock | <kbd>Shift</kbd>+<kbd>L</kbd> |
| Frame Selection | <kbd>Ctrl</kbd>+<kbd>E</kbd> |
| Show Grid | <kbd>Shift</kbd>+<kbd>R</kbd> |
| Snap to Grid | <kbd>Shift</kbd>+<kbd>W</kbd> |
| Next 2D View | <kbd>Tab</kbd> |
| Maximize or Restore Active View | <kbd>Shift</kbd>+<kbd>Z</kbd> |
| Build and Launch | <kbd>F9</kbd> |
| Isolate Selection | <kbd>Ctrl</kbd>+<kbd>H</kbd> |
| Invert Selection | <kbd>Shift</kbd>+<kbd>I</kbd> |
| Show All Hidden | <kbd>U</kbd> |
| Rotate 90 Degrees Left / Rotate 90 Degrees Right | <kbd>Ctrl</kbd>+<kbd>R</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>R</kbd> |

**Duplicate Selection**, **3D Wireframe** and **Draw Sector** have no key. JMF files, detail and structural modes, texture-tool function keys and texture application gestures are not reproduced.

</details>

<details>
<summary>Sledge Style</summary>

ID `sledge`, also `sledge-editor`, `sledge2`, `sledge-2`. Four views: camera, top, front and side, 16-unit grid. 3D camera: first-person, 60 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Middle drag; hold <kbd>Space</kbd> and move the pointer; <kbd>Shift</kbd>+arrow keys pan a quarter of the view | — |
| Zoom | Wheel | — |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click adds one | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click picks a face |
| Select by area | Left drag over empty space | — |
| Move the selection | Arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles | — |
| Look around | — | <kbd>Z</kbd> turns mouse look on or off; hold <kbd>Space</kbd> to look until you let go; while looking, the right button pans and both buttons move along the view |
| Move forward and back | — | Wheel |
| Fly | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right, <kbd>Q</kbd> up, <kbd>E</kbd> down; <kbd>Shift</kbd> faster, <kbd>Ctrl</kbd> slower (the speed keys work only while looking) |
| Drive | — | <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right, <kbd>Up</kbd> look up, <kbd>Down</kbd> look down; <kbd>Shift</kbd>+turn keys strafe and <kbd>Shift</kbd>+look keys move up or down |

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Select None | <kbd>Shift</kbd>+<kbd>Q</kbd> |
| Clip Tool | <kbd>Shift</kbd>+<kbd>X</kbd> |
| Carve | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> |
| Hollow | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>H</kbd> |
| Isolate Selection | <kbd>Ctrl</kbd>+<kbd>H</kbd> |
| Show All Hidden | <kbd>U</kbd> |
| Texture Lock | <kbd>Shift</kbd>+<kbd>L</kbd> |
| Show Grid | <kbd>Shift</kbd>+<kbd>R</kbd> |
| Snap to Grid | <kbd>Shift</kbd>+<kbd>W</kbd> |
| Maximize or Restore Active View | <kbd>Shift</kbd>+<kbd>Z</kbd> |
| Build and Launch | <kbd>F9</kbd> |

**Duplicate Selection**, **3D Wireframe** and **Draw Sector** have no key. Navigation speeds, zoom presets, linked wheel zoom, duplicate-on-drag, grouping and tool modes differ from Sledge, and its preferences and RMF or VMF files are not imported.

</details>

<details>
<summary>Doom Builder 2 / X Style</summary>

ID `doom-builder`, also `doom-builder-2`, `doombuilder`, `doombuilder2`, `doom-builder-x`, `db2`, `dbx`. One view, 2D first, 32-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Middle drag | Middle drag |
| Zoom | Wheel | — |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click adds one | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click picks a face |
| Select by area | Left drag over empty space | — |
| Move the selection | Arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles | — |
| Look around | — | Right drag |
| Move forward and back | — | Wheel |
| Fly | — | <kbd>E</kbd> forward, <kbd>D</kbd> back, <kbd>S</kbd> left, <kbd>F</kbd> right; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Select None | <kbd>C</kbd> |
| Draw Sector | <kbd>Ctrl</kbd>+<kbd>D</kbd> |
| 3D View | <kbd>W</kbd> |
| Build and Launch | <kbd>F9</kbd> |

**Duplicate Selection**, **3D Wireframe** and **Clip Tool** have no key. Crosshair editing, right-drag movement, gravity, the V, L, S and T modes and wheel height editing are not reproduced; painting and sampling are separate tools.

</details>

<details>
<summary>Ultimate Doom Builder Style</summary>

ID `ultimate-doom-builder`, also `udb`, `gzdoom-builder`, `gzdb`, `gzdoom-builder-bugfix`. One view, 2D first, 32-unit grid. 3D camera: first-person, 90 degree field of view.

Mouse gestures and camera keys are the same as **Doom Builder 2 / X Style**.

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Select None | <kbd>C</kbd> |
| Draw Sector | <kbd>Ctrl</kbd>+<kbd>D</kbd> |
| 3D View | <kbd>Q</kbd> |
| Build and Launch | <kbd>F9</kbd> |

**Duplicate Selection**, **3D Wireframe** and **Clip Tool** have no key. Slopes, 3D floors, gravity and source-port visual effects are not shown.

</details>

<details>
<summary>SLADE Style</summary>

ID `slade`, also `slade3`, `slade-3`. One view, 2D first, 32-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Right drag | Middle drag |
| Zoom | Wheel | — |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click adds one | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click picks a face |
| Select by area | Left drag over empty space | — |
| Move the selection | Arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles | — |
| Look around | — | Right drag |
| Move forward and back | — | Wheel |
| Fly | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right, <kbd>Up</kbd> up, <kbd>Down</kbd> down; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |
| Drive | — | <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right; while looking, the turn keys strafe |

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Select None | <kbd>C</kbd> |
| 3D View | <kbd>Q</kbd> |
| Snap to Grid | <kbd>Shift</kbd>+<kbd>G</kbd> |
| Zoom In / Zoom Out | <kbd>=</kbd> / <kbd>-</kbd> |

**3D Wireframe**, **Draw Sector** and **Clip Tool** have no key. Painting and sampling are separate tools. The V, L, S and T modes, visual-mode texture gestures and wheel height changes are not reproduced.

</details>

<details>
<summary>Eureka Style</summary>

ID `eureka`, also `eureka-doom`. One view, 2D first, 16-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Middle drag | Middle drag |
| Zoom | Wheel | — |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click adds one | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click picks a face |
| Select by area | Left drag over empty space | — |
| Move the selection | Arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles | — |
| Look around | — | Right drag |
| Move forward and back | — | Wheel |
| Fly | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right, <kbd>Page Up</kbd> up, <kbd>Page Down</kbd> down; <kbd>Alt</kbd> faster, <kbd>Shift</kbd> slower |
| Drive | — | <kbd>Up</kbd> forward, <kbd>Down</kbd> back, <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right; while looking, the turn keys strafe |

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Duplicate Selection | <kbd>O</kbd> |
| 3D View | <kbd>Tab</kbd> |
| Zoom In / Zoom Out | <kbd>=</kbd> / <kbd>-</kbd> |
| Grid 2 to 1024 | <kbd>1</kbd> to <kbd>9</kbd>, then <kbd>0</kbd> |

**3D Wireframe** and **Draw Sector** have no key. As in Eureka, <kbd>9</kbd> chooses **Grid 512** and <kbd>0</kbd> **Grid 1024**. Letter-held scrolling, right-button line drawing and the V, L, S and T modes are not implemented.

</details>

<details>
<summary>Unreal Editor Style</summary>

ID `unreal`, also `unreal-editor`, `ue4`, `ue5`. One view, 3D first, 16-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Middle drag, right drag | Middle drag |
| Zoom | Wheel | — |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click adds one | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click picks a face |
| Select by area | Left drag over empty space | — |
| Move the selection | Arrow keys nudge it | — |
| Resize the selection | Left drag its handles | — |
| Look around | — | Right drag |
| Orbit | — | <kbd>Alt</kbd>+left drag |
| Move forward and back | — | Wheel |
| Fly while looking | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right, <kbd>E</kbd> up, <kbd>Q</kbd> down; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Duplicate Selection | <kbd>Ctrl</kbd>+<kbd>W</kbd> |
| Frame Selection | <kbd>F</kbd> |
| Show All Hidden | <kbd>Ctrl</kbd>+<kbd>H</kbd> |

**3D Wireframe** and **Draw Sector** have no key. Moving and resizing use VibeStudio's handles and numeric tools. Gizmo modes, left-button camera driving, wheel flight-speed changes and Unreal assets are not reproduced.

</details>

<details>
<summary>Unity Scene View Style</summary>

ID `unity`, also `unity-editor`, `unity-scene-view`. One view, 3D first, 16-unit grid. 3D camera: first-person, 90 degree field of view.

Mouse gestures and camera keys are the same as **Unreal Editor Style**.

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Frame Selection | <kbd>F</kbd> |

**3D Wireframe** and **Draw Sector** have no key. <kbd>Alt</kbd>+right zoom, tool modes, play mode and Unity assets are not reproduced.

</details>

<details>
<summary>Godot 3D Style</summary>

ID `godot`, also `godot-3d`, `godot-editor`. One view, 3D first, 16-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Middle drag, right drag | <kbd>Shift</kbd>+middle drag |
| Zoom | Wheel | — |
| Select | Left click; <kbd>Shift</kbd>+left click adds or removes one | Left click; <kbd>Shift</kbd>+left click adds or removes one; <kbd>Ctrl</kbd>+left click picks a face |
| Select by area | Left drag over empty space | — |
| Move the selection | Arrow keys nudge it | — |
| Resize the selection | Left drag its handles | — |
| Look around | — | Right drag; <kbd>Shift</kbd>+<kbd>F</kbd> turns mouse look on or off |
| Orbit | — | Middle drag |
| Move forward and back | — | Wheel |
| Fly while looking | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right, <kbd>E</kbd> up, <kbd>Q</kbd> down; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Frame Selection | <kbd>F</kbd> |
| Top View / Front View / Side View | <kbd>Keypad 7</kbd> / <kbd>Keypad 1</kbd> / <kbd>Keypad 3</kbd> |
| Snap to Grid | <kbd>Y</kbd> |

**3D Wireframe** and **Draw Sector** have no key. Godot nodes, gizmos, keypad perspective switching and wheel flight-speed changes are not reproduced.

</details>

<details>
<summary>Blender Style</summary>

ID `blender`, also `blender-3d`. One view, 3D first, 16-unit grid. 3D camera: first-person, 90 degree field of view.

Mouse gestures and camera keys are the same as **Godot 3D Style**, except that <kbd>Shift</kbd>+<kbd>&#96;</kbd> turns mouse look on or off.

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Duplicate Selection | <kbd>Shift</kbd>+<kbd>D</kbd> |
| Select All | <kbd>A</kbd> |
| Select None | <kbd>Alt</kbd>+<kbd>A</kbd> |
| Frame Selection | <kbd>Keypad .</kbd> |
| Top View / Front View / Side View | <kbd>Keypad 7</kbd> / <kbd>Keypad 1</kbd> / <kbd>Keypad 3</kbd> |
| Snap to Grid | <kbd>Shift</kbd>+<kbd>Tab</kbd> |
| Show All Hidden | <kbd>Alt</kbd>+<kbd>H</kbd> |

**3D Wireframe** and **Draw Sector** have no key. Mesh modes, <kbd>G</kbd>/<kbd>R</kbd>/<kbd>S</kbd> modal transforms, walk gravity and .blend files are not reproduced.

</details>

### DoomEdit and BSP controls

**DoomEdit Style** (`doomedit`, also `doom-edit`, `doom3-radiant` or `d3radiant`)
uses classic Radiant plan editing with right-drag camera steering, Ctrl+right
panning and Ctrl+Shift+right mouse look. Shift+M merges brushes, Ctrl+Shift+H
isolates the selection, 0 toggles the grid, and Home or Ctrl+Tab changes the plan
projection. End levels the camera. S opens Surface Alignment and Shift+S opens patch editing. Doom 3
formats, rendering, light/material tools, floor stepping and native texture
gestures are not added by this profile.

**BSP Quake Editor Style** (`bsp`, also `bsp-editor`, `bsp-quake-editor` or `bsp97`) uses
middle-drag mouse look, Shift+middle panning and right-click material sampling.
WASD moves, R/F rises or sinks, and Q/E turns. Ctrl+Space duplicates, Ctrl+X or
keypad minus deletes, backtick selects all, Z opens Surface Alignment and Alt+S
snaps to the grid. Shift selects in plans; a bare drag draws or resizes. The
adapted VibeStudio workspace starts with four views and a 16-unit grid. Native
selection cycling, texture drags, region tools and configuration import are not
reproduced.

Both appear in **Browse Editor Profiles…**, whose preview lists the complete
implemented defaults. Hammer++ names remain aliases for classic Hammer controls;
Hammer++ extensions and Hammer 2 do not have separate implemented presets.

### Learn more

- [Level editing](manual/levels.md)
- [Editor profile design notes and coverage](EDITOR_PROFILES.md)
- [Editor workflow credits and reference revisions](CREDITS.md#editor-workflow-inspirations)

#### Select siblings in the TB profile

**Ctrl+B** runs **Select Siblings** in Levels. **Run Compiler Profile** uses
**Ctrl+Alt+B** in this profile. Other profiles retain their compiler keys and
expose sibling selection through the menus, tool shelf and command search.
Both sibling and source-line selection follow the profile's object-selection
history policy; [the selection manual](manual/levels.md#select-by-hierarchy-or-source-line)
describes group boundaries and source positions.

#### Clipboard placement keys

Ctrl+V places at the cursor in the TB profile and at saved coordinates in other
profiles. TB's Ctrl+Alt+V selects **Paste at Original Position**. NRC's Shift+V
selects **Paste to Camera**, using the snapped translation from assembly centre
to camera, and Ctrl+Shift+V moves the existing selection there with the same
rule (**Move to Camera**). The explicit commands remain available in every
profile, and user shortcut overrides take precedence. See
[Clipboard placement](LEVEL_PASTE.md).


TB **Ctrl+C** copies picked face attributes when faces are picked, otherwise
objects; **Ctrl+X** is disabled for face picks. NRC and other profiles keep object
copy. All profiles can use **Copy Surface**, then normal paste for face text.
Paste applies attributes to the current face/brush selection without moving it.
The profile control reference lists this copy policy alongside paste placement.


**Select by Region** also follows your profile. TB removes the selected brushes
after testing their convex volumes; NRC Inside removes selected objects, while
Touching keeps them. The default profile retains geometry. The controls reference
and command tooltips describe this distinction. See
[Select by brush volume](manual/levels.md#select-by-brush-volume) for undo and shortcuts.

**Temporary visibility** follows the profile too. TB records Hide, Show All,
Isolate and object-list reveal as non-content undo steps. NRC and default
controls keep hiding outside history. Show All leaves saved scene visibility
and filters in effect; the context menu shows the current profile's bindings.
See [Temporary visibility](manual/levels.md#temporary-visibility).

NRC **Ctrl+Shift+E** invokes **Select Connected Entities**, adding direct incoming
and outgoing neighbours. Other profiles expose the command in menus and Tools;
TB retains **Select Complete Tall** on that key. The command follows each
profile's selection-history policy. See
[Select connected entities](manual/levels.md#select-connected-entities).

NRC **Shift+A** runs **Select All of Type**, using all selected entity owners or
the current material. Face mode selects matching brush faces; vertex and edge
modes leave picks unchanged. **Select All of This Class** remains separate,
and other profiles retain their existing class-selection keys. See
[Select all of a type](manual/levels.md#select-all-of-a-type).

## Packages

The Packages page opens game archives and folders so you can browse, preview, extract, check and
compare their contents, and build new packages from them. Every edit is staged first: the package you
opened stays unchanged until you save, and saving writes a new file unless you choose to replace one.

> [!NOTE]
> **Status: Available.** Browsing, previews, extraction, validation, comparison, staged edits and
> Save As work for PAK, WAD, ZIP and PK3 archives and for folders, with automated tests but little
> real-world use.
> Encrypted ZIP entries and compressed WAD2/WAD3 lumps are listed but not read, and very large
> packages have not been tested at scale.

### Supported formats

| Format | Open | Save | Notes |
| --- | --- | --- | --- |
| Folder | Yes | No | Save the staged result as an archive instead. |
| Quake PAK (`.pak`) | Yes | Yes | Always stored uncompressed. |
| Doom IWAD and PWAD (`.wad`) | Yes | Yes | Keeps the lump order, including repeated map names. Lump names are at most 8 characters. |
| Quake WAD2, Half-Life WAD3 (`.wad`, `.wad2`, `.wad3`) | Yes | Yes | Compressed lumps are listed but not read; saved lumps are uncompressed. |
| ZIP and PK3 (`.zip`, `.pk3`, also `.pk4`, `.pkz`) | Yes | Yes | Stored and DEFLATE entries. Encrypted entries and other compression methods are listed but not read; multi-disk archives are refused. |
| Package draft (`.vibepackage`) | Yes | Yes | Your staged edits and their history, described below. |

### Open a package or folder

- Choose **Open Package** on the Packages header, or **File** > **Open Package…**
  (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>O</kbd>), and pick an archive.
- Choose **Open Folder**, or **File** > **Open Folder Package…**
  (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>O</kbd>), to treat a folder on disk as a package.
- Drop an archive onto the window, or reopen one from **File** > **Open Recent**.

On macOS, press <kbd>Cmd</kbd> wherever this page says <kbd>Ctrl</kbd>. Opening runs in the
background with progress and **Cancel**, and the Activity Center records the result. A package with
more than 250,000 entries, including folders, is refused rather than half loaded. **File** >
**Close Package** (<kbd>Ctrl</kbd>+<kbd>W</kbd>) closes it.

**New Package** starts an empty document instead. Choose its format: **PK3 package**, **ZIP archive**,
**Quake PAK**, **Doom PWAD**, **Doom IWAD**, **Quake WAD2** or **Half-Life WAD3**.

### Browse the contents

The page has three columns. **Folders** on the left is the package's folder tree. The entry list in
the middle shows the current folder, with back, forward and up buttons and a clickable folder path
above it. The inspector on the right has four tabs:

- **Details**: what is known about the selected entry, such as image size, model frames or sound format.
- **Preview**: the decoded pixels of an image, sprite or texture, or the text of a text entry.
- **Staging**: your staged changes, described in [Stage changes](manual/packages.md#stage-changes).
- **Overview**: **Composition By Type**, a chart of how the package divides by type and size.
  Select a slice to filter the list to that type.

Double-click an entry, or press <kbd>Enter</kbd>, to open it where it belongs: images on Textures,
models on Models and sounds on Audio. Maps, shader scripts, entity definitions and text files open
from a temporary copy; text opens read-only on the Code page, because edits to a copy never reach the
package. Right-click an entry for **Open**, **Extract Selected…**, **Stage Replace…**,
**Stage Rename…**, **Stage Delete**, **Unstage** and **Copy Virtual Path**.

Previews are prepared in the background. **Cancel Preview** stops one and **Retry Preview** reads it
again. Large entries are previewed from a sample, which does not prove the whole entry is intact; use
[Validate](manual/packages.md#check-package-integrity) for that.

### Filter the entry list

Type in the filter field above the list (its placeholder reads "Filter entries by path or type"). A
plain word keeps entries whose path or type contains it. Property tests narrow the list further, and
every term you type must hold.

| You type | Keeps entries where |
| --- | --- |
| `ext=wav` | the property equals the value, ignoring case |
| `folder:textures` | the property contains the text |
| `ext!=tga` | the property does not equal the value |
| `size>1mb`, `size<=64k` | a number compares; sizes take `k`, `kb`, `m`, `mb`, `g` or `gb`, counted in 1024s |
| `name:"stone wall"` | quotes hold a value with spaces |

The keys are `path`, `name`, `ext`, `folder`, `type`, `storage`, `kind`, `size` and `packed` (the
compressed size). Types read like `image/png`, `audio/wav` or `text/shader`, and Doom WAD lumps use
types such as `wad-flat`, `wad-sprite`, `wad-patch` and `wad-sound`, so `type:image` keeps every image.

Press <kbd>Enter</kbd> in the filter to select every match and move to the list, where extraction
and staging act on the whole selection. Press <kbd>Down</kbd> in the filter to reuse an earlier
query. The same syntax works in `vibestudio --cli package list <package> --where "<query>"`, and in
other lists such as **Objects** on the Levels page.

### Extract files

1. Select entries in the list.
2. Choose **Extract** on the toolbar (<kbd>Ctrl</kbd>+<kbd>E</kbd>), or **Project** >
   **Extract All Entries…** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>E</kbd>) for everything.
3. Choose an output folder.
   A dialog shows the current file and its progress.

Extraction never overwrites existing files: they are skipped. Unsafe names, such as absolute paths or
`..`, and links in the output path are refused. When two selected entries would land on the same
name, as repeated WAD lumps do, **Extraction Paths** lets you give each a separate path.
**Cancel Extract** stops at once: files already extracted stay on disk and the unfinished one is
discarded. You can also drag a few entries out of the list into another folder or program; use
**Extract** for large batches.

### Check package integrity

Choose **Validate** on the Packages header. VibeStudio reads every entry, including each copy of a
repeated WAD lump, checks its size and checksum, and records a SHA-256 hash. A package passes only
when every file verifies with no warnings. **Export JSON…** saves the report. Validation checks the
container, not whether each asset works in the game.

### Compare two packages

Choose **Compare** on the Packages header and pick another archive. VibeStudio pairs entries by path,
ignoring case, and reports added, removed, changed, case-only and unchecked entries; files of equal
size are hashed to prove they match. Search the results, select a row for its paths and hashes, or
choose **Export JSON…**. Comparison and validation both run in the background and can be cancelled.

### Stage changes

Staged changes build a plan for the next save. The list and folders show them at once, but nothing is
written until you save.

| To | Do this |
| --- | --- |
| Add a file | **Project** > **Stage Add File…** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>A</kbd>), then choose the file and its path in the package. |
| Replace an entry | Select it, then **Project** > **Stage Replace…** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>R</kbd>). |
| Rename an entry | Select it, then **Project** > **Stage Rename…** (<kbd>F2</kbd>). |
| Delete entries | Select them, then **Project** > **Stage Delete** (<kbd>Del</kbd>). |
| Make or change folders | **Project** > **New Package Folder…**, or right-click a folder for **Rename Folder…** and **Delete Folder**. |
| Rename or remove Doom maps | **Project** > **WAD Groups…**, which keeps each map's lumps together. |
| Undo or redo | **Edit** > **Undo Package Edit** (<kbd>Ctrl</kbd>+<kbd>Z</kbd>) and **Redo Package Edit** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd>). |

The same commands sit as icon buttons in the toolbar's **Stage** group; hover over one to see what it
does. The keys act only while the Packages page has focus. VibeStudio keeps its own copy of each file
you add, so changing the original afterwards does not change the staged entry. ZIP and PK3 keep empty
folders. A PAK cannot store them, so saving a PAK with an empty folder is refused. WAD has no folders.

The **Staging** tab lists every change with its conflicts and blockers; right-click one for
**Unstage** or **Show Entry**. Before saving, choose **Review Changes** to compare the staged result
with the source, entry by entry, with the same search and **Export JSON…** as **Compare**. Renaming
or deleting an entry does not update references to it in maps, scripts or shaders, so check those
before you publish.

### Save a new package

1. Choose a **Compression** level: **Store (no compression)**, **Fast**, **Default** or **Best**.
   It applies to ZIP and PK3; PAK and WAD are always stored.
2. Choose **Save As** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd>) and enter a file name ending in
   `.pk3`, `.zip`, `.pak` or `.wad`. The extension chooses the format.
   A dialog shows progress and offers **Cancel**.
3. When the save finishes, the new archive becomes the open package. An open draft stays open
   instead, with its history.

**Include Staging Manifest**, in the menu beside **Save As** and on by default, also writes
`<name>.manifest.json` with the hashes of every original and final entry. A failed or cancelled save
keeps your staged changes so you can try again.

Output checks also recognise hard links to protected archive, folder and draft
inputs. A destination outside the source folder can still refer to a source
file this way. Checking an existing linked destination may scan protected folder
contents, with progress and cancellation. If the scan cannot be completed or
exceeds its limits, choose a new independent output. Extraction uses the same
source protection. These checks do not reserve the filesystem against changes
by other applications.

#### Replace an existing package

Saving over a file that already exists, including the open package, is opt-in. VibeStudio asks
**Replace Existing Package?** first. If you agree, it writes the new archive beside the old one,
verifies it, and only then moves the original to `<file>.bak`. If a save is interrupted, **File** >
**Recover Packages…** > **Interrupted Saves…** checks the files left behind. When the new package is
already in place and verified, **Finish Reviewed Save…** completes the backup and cleans up.

### Keep work in progress

**File** > **Save Package Draft** (<kbd>Ctrl</kbd>+<kbd>S</kbd> on the Packages page, also an icon on
the toolbar) stores the source content, your staged changes and their undo history in a `.vibepackage`
folder. **File** > **Open Package Draft…** resumes it later, even without the original files; move or
copy the whole folder. VibeStudio also keeps local recovery checkpoints every 30 seconds by default,
and **File** > **Recover Packages…** restores one as a new draft.

### Export part of a package

Select files or folders and choose **Export Selected** on the Packages header to save them as a
separate package. Doom map lumps travel with their whole map. To collect the textures, models and
sounds a map uses, open the map and its package and choose **Dependencies** on the Levels header,
which can export the resolved assets; see [Level editing](manual/levels.md).

To release your own project, map, model or textures for players, with only your own files, release
notes and a readme, use **Package and Release** instead; see [Package and release](manual/releases.md).

### Command-line equivalents

| Task | Command |
| --- | --- |
| Summarise or list | `vibestudio --cli package info <package>`, `package list <package> --where "<query>"` |
| Preview one entry | `vibestudio --cli package preview <package> <entry>` |
| Extract | `vibestudio --cli package extract <package> --output <folder>` |
| Validate | `vibestudio --cli package validate <package>` |
| Compare | `vibestudio --cli package compare <left> <right>` |
| Preview staged edits | `vibestudio --cli package stage <package> --add-file <file> --as <entry>` |
| Save a new package | `vibestudio --cli package save-as <package> <output> --format pk3` |
| Start an empty package | `vibestudio --cli package create <output> --format pk3` |
| Save part of a package | `vibestudio --cli package subset <package> <output> --prefix <folder>` |

For example:

```sh
vibestudio --cli package list ./baseq3/pak0.pk3 --where "type:image size>256k"
vibestudio --cli package validate ./release.pk3 --json
vibestudio --cli package extract ./pak0.pak --output ./out --entry maps/start.bsp --dry-run
vibestudio --cli package compare ./release-1.pk3 ./release-2.pk3 --json
vibestudio --cli package save-as ./mod-folder ./build/mod.pk3 --format pk3 --add-file ./autoexec.cfg --as scripts/autoexec.cfg
```

`package validate` and `package compare` exit with code 4 when they find a problem or a difference.
`package save-as` refuses to write over its source unless you pass `--in-place`, which keeps the
original at `--backup` (by default `<output>.bak`). Add `--dry-run` to see what a write would do.

### Learn more

- [Package and release](manual/releases.md): package your own work for players.
- [Package manager](PACKAGE_MANAGER.md): limits, drafts, recovery and every staging rule.
- [Support matrix](SUPPORT_MATRIX.md#archive-and-package-formats): exact format support.
- [CLI strategy](CLI_STRATEGY.md): every `package` option.

## Package and release

**Package and Release** turns your project, a map, a model or a set of textures into something players
can install. VibeStudio works out which files are yours and which the game already has, gathers every
custom texture, shader, model, sound and script your work uses, and writes the package with a readme,
release notes and a distribution archive. It keeps a changelog and a record of every release, so the
next one can say what changed.

> [!NOTE]
> **Status: Partial.** Planning, release notes and publishing work for Quake, Quake II, Quake III
> Arena, Doom, Heretic and Hexen projects, with automated tests built from generated files. They have
> not been tried with real game installations or real projects yet. A release finds the files that
> maps, models and shader scripts name, and those that QuakeC, Quake III menus and configs, and
> DECORATE, ZScript, SNDINFO and MAPINFO name in full (see
> [Files that game code and scripts load](manual/releases.md#files-that-game-code-and-scripts-load)). Files that
> compiled game code loads, or whose names a script builds while the game runs, must be added by
> hand. Doom 3-era games are not indexed automatically.

### Before you start

1. Open your project on the Workspace page (see [Projects and game installations](manual/projects.md)).
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

### Package a map

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
- the files the map's custom monsters and items load: the models and sounds a QuakeC spawn function
  precaches, or a DECORATE or ZScript actor's sprites and sounds, with the WAD or script that
  defines the actor (see [Files that game code and scripts load](manual/releases.md#files-that-game-code-and-scripts-load));
- for Doom-family maps, the map's lumps and the custom textures, flats, patches, sprites, sounds and
  music it needs, merged into one WAD.

Files the game provides stay out. A project file with the same path as one of the game's own files
ships, because it replaces the game's version; the review marks it so you can decide whether that is
what you want. An identical copy of a stock file stays out.

### Package a project, model or textures

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

### Review what ships

The review beside the options updates as you change them:

- The status line says **Ready to publish** or **Not ready**, with the file count, size and package
  name.
- Chips count **Included** files, files **From the game**, files that **Replace** the game's own, and
  **Problems**. Select a chip to open its list.
- **Contents by kind** charts the release by maps, textures, shaders, models, sounds and other files.
- **Included** lists every file with its kind, size and the map, model or script that needs it. A
  warning icon marks a file that replaces one of the game's; a muted icon marks a file that ships
  beside the package, such as native game code, which engines cannot load from a package. A file's
  tooltip, which screen readers read as its description, names the scripts that load it and the
  line in each.
- **From the Game** lists the references the game provides and which of its packages holds each.
- **Problems** lists what blocks publishing first, such as a missing texture or a map that has not
  been built, then advisories, such as a sound that a script names but neither the project nor the
  game has. Activate an unbuilt or out-of-date map to open it on the Build page.
- **Details** is the whole plan as text.

### Files that game code and scripts load

Maps and models are not the only things that name files. VibeStudio also reads the project's game
code and scripts as text, without running them, and follows the file names written out in them:

| Source | What it follows |
| --- | --- |
| QuakeC (`.qc`, `.qh`) | `precache_model`, `precache_sound` and `precache_file`, and the models and sounds passed to `setmodel`, `sound` and `ambientsound` |
| Quake III menus (`.menu`, and `ui/menus.txt`) | Backgrounds, cursors, models, cinematics and menu sounds, the sounds actions play, the menus `loadMenu` loads, `#include` files, and the configs actions run |
| Configs (`.cfg`, `quake.rc`) | `exec` chains, the sounds `play` plays, Quake III's `music`, maps that `map` starts, and the commands bound to keys or kept in aliases |
| DECORATE and ZScript | Each actor's sprites, sounds and icon, the actors it fires, drops or spawns, and `#include` files |
| SNDINFO | The lump or file each sound name plays, including `$random` and `$alias` |
| MAPINFO and ZMAPINFO | Each map's music and sky, title and intermission pictures, `DoomEdNums` and `include` files |

What happens to the files they name depends on what you release:

- **Whole project.** The project's files ship anyway. The review names the script behind each one
  in **Included**, lists names the game provides under **From the Game**, and lists names that
  nothing provides, or that the release leaves out, under **Problems** as advisories.
- **Maps.** The map's own entities decide. A monster or item that the project's QuakeC defines
  brings the files its spawn function, and the functions it calls, precache. A Doom thing whose
  editor number belongs to a DECORATE or ZScript actor, or a standard Doom monster or item that such
  an actor replaces, brings the actor's sprites and sounds, the actors it fires or drops, and the WAD
  or script that defines them. The map's MAPINFO entry brings its music and sky. A map whose
  entities need the mod's compiled QuakeC says so in the review: release the whole project to ship
  `progs.dat` with it.

A ZDoom sound name that the project's SNDINFO does not define is taken to be one of the engine's
own and is not checked.

### Choose what ships

Some files still cannot be found by reading the project: those that compiled game code (QVM, DLL,
`progs.dat`) or ACS loads, Quake II and Quake III game code written in C, and any file whose name a
script builds while the game runs. The project's release settings handle them:

- **Include sources** ships the `.map` files and source art too.
- In the project manifest, `release.include` lists patterns that always ship, and `release.exclude`
  patterns that never do, such as `"docs/*.txt"` or `"**/*.psd"`. See
  [Release settings](manual/releases.md#release-settings).

Some files never ship from a project: sources such as `.map`, `.psd` and `.blend` (unless you include
sources), compiler leftovers such as `.prt`, `.lin`, `.log` and `.bak`, programs, the project's
`.vibestudio` folder, its output and temporary folders, and Quake-family texture WADs, which the
compiled BSP already contains.

### Describe the release

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

### Write the release notes

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

### Publish

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

### Release history

The Workspace page's **Releases** card shows the number of unreleased changes and the latest
releases, with their package, date and size. Activate a release to open its folder. The next release
compares itself with the latest one recorded, so its notes can list what was added, updated and
removed.

### Check a map's dependencies

**Dependencies** on the Levels header lists what the open map uses, also when no package is open: it
then reads the project's folders, as a release does, and offers **Package Map…** in place of
**Export Assets…**. With an indexed installation, references the game provides show as **Provided by
the game**. See [Level editing](manual/levels.md#check-maps-and-entities).

### Requirements and expansions

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

### Release settings

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

### Command-line equivalents

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
check. With `--json`, `release plan` lists each name that game code and scripts use under
`scriptReferences`, with the script, the line and what the name resolved to. `release plan` and
`install register info` exit with code 4 when something needs attention.
`release publish` writes nothing with `--dry-run`, and `--no-archive`, `--no-readme`, `--no-notes`,
`--no-record` and `--no-changelog` leave those files out.

### Learn more

- [Project releases](PROJECT_RELEASES.md): how releases are planned, the asset index format and
  every rule.
- [Packages](manual/packages.md): open, check and edit packages by hand.
- [CLI strategy](CLI_STRATEGY.md): every `release` and `install register` option.

## Textures and sprites

The **Textures** page lists the images in a package, decodes them with the
right palette, and opens the Texture Editor for painting, converting and
staging textures for Doom, Quake, Quake II and Quake III projects.

The Texture Editor opens in a tab within **Textures**. **Browser** returns to
the image list without closing the texture document.

> [!NOTE]
> **Status: Partial.** Decoding the classic idTech image formats, the Texture
> Editor and its game export profiles work and have automated tests, but they
> have not been proven in real projects yet. Sprite files cannot be written,
> and WAD3 export and Doom wall-texture definitions are not built.

### Browse textures

Choose **Textures** on the rail, or press <kbd>Ctrl</kbd>+<kbd>4</kbd>
(<kbd>Cmd</kbd> on macOS). The page lists the images in the open package, so
open one first:

- **File** > **Open Package…** opens a PAK, WAD, ZIP or PK3 file.
- **File** > **Open Folder Package…** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>O</kbd>)
  treats a folder, such as your project's asset folder, as a package.
- **Go to File** (<kbd>Ctrl</kbd>+<kbd>P</kbd>), or dropping a loose image on
  the window, opens the image's folder as a package and selects the image.

Opening a project does not list its images by itself; the project supplies the
palette instead (see [Choose a palette](manual/textures.md#choose-a-palette)).

Switch between **Tiles** and **List**, and type in the filter to search paths.
Terms such as `ext=wal`, `size>16kb` or `w>=128 format=wal` test the name,
extension, folder, size and number of uses in the open map, and, once an image
is decoded, its width, height and format. Tick **In open map** to list only the
textures the map open in **Levels** uses.

Select an image to decode it. Decoding runs in the background:
**Cancel Preview** stops it, and **Reload Previews** rereads images and
palettes from the package. In the preview, scroll to zoom, drag to pan and
point at a texel to see its palette index. **Zoom to Fit** (<kbd>F</kbd>),
**Actual Size** (<kbd>0</kbd>), **Zoom In** (<kbd>+</kbd>), **Zoom Out**
(<kbd>-</kbd>), **Pixel grid** and **Transparency** (a checkerboard behind
transparent texels) sit above it. **Mip** shows another mip level and
**Sprite frame** steps through a sprite's frames.

The inspector has three tabs: **Details** (format, size, mip levels, palette
source, flags and raw metadata), **Palette** (all 256 swatches, with the
transparent index marked) and **Sprite Creator**.

### Supported image formats

| Format | Typical use | Notes |
| --- | --- | --- |
| Doom patches, flats, `PLAYPAL`, `COLORMAP` | Doom, Heretic, Hexen WADs | Patches keep their offsets. Flats are recognised by size (4096, 4160 or 16384 bytes) in a flat namespace or folder. |
| Quake `.lmp` pictures, WAD2 miptextures | Quake | Miptextures decode all four mip levels. |
| WAD3 miptextures | Half-Life-style WADs | Each texture carries its own palette. There is no WAD3 texture export. |
| `.wal`, `.m8`, `.m32` | Quake II engine games | WAL surface flags, content flags and animation names are read. |
| PCX, TGA | Quake II skins and pictures, Quake III | 8-bit PCX only. |
| `.spr`, `.sp2` | Quake, Half-Life and Quake II sprites | See [Work with sprites](manual/textures.md#work-with-sprites). |
| DDS, FTX, SiN `.swl` | Exchange with other tools | DDS shows its base mip level only. There is no SWL export. |
| PNG, JPEG, BMP, GIF, TIFF, WebP | Quake III and source ports | Read through Qt's image plugins, so they depend on your build. |

Images up to 64 MiB decode; one that fails shows the reason instead of a
partial picture.

### Choose a palette

Indexed formats store palette numbers rather than colours, so how they look
depends on the palette. The **Palette** list on the toolbar decides it:

- **Automatic**, the default, uses the palette your project names in its
  manifest (see [Projects](manual/projects.md)), otherwise the one the open package
  ships: `PLAYPAL` for Doom-family WADs, `gfx/palette.lmp` for Quake and
  `pics/colormap.pcx` for Quake II.
- The other entries name a game family, such as **Quake II (generated)** or
  **Doom (generated)**. VibeStudio still looks for that family's real palette,
  first in the open package and then in the selected game installation.

The line under the preview always says where the palette came from. If no real
palette is found, VibeStudio uses a generated stand-in palette and says so:
indexed colours will not match the game. The stand-ins contain no game data.
When neither the project nor package names a palette family, **Automatic** can
infer it from the selected image, such as a Doom flat, before checking the
installation. Its resolved source stays visible when you return to the browser.

### Use a texture in the open map

Right-click an image to use it in the map open in **Levels**. These actions
change the map in memory; save it in **Levels** afterwards (see
[Level editing](manual/levels.md)).

| Action | What it does |
| --- | --- |
| **Select in Open Map** | Selects everything in the map that uses the texture. |
| **Apply to Map Selection** | Puts the texture on every face of the brushes and patches selected in a Quake-family map, as one undoable edit. |
| **Use for Map Painting** | Makes it the paint material and switches **Levels** to its paint tool. |
| **Use in Open Map…** | Replaces a texture the map already uses with this one. |
| **Show in Packages** | Shows the exact package entry. |
| **Show in Materials** | Lists the shaders and materials that read the image on the **Materials** page, drawn the way the game draws them (see [Materials and shaders](manual/materials.md)). |
| **Export PNG…** | Writes the decoded image to a PNG file. |

### Edit a texture

1. Choose **Texture Editor** on the page header to start a new image, or select
   an image and choose **Edit Selected** to edit the mip level or sprite frame
   on screen, keeping its name, WAL flags or patch offsets for export.
   **Open…** in the editor imports a loose image file instead.
2. Pick a section from the editor's inspector list: **Paint**, **Transform**,
   **Selection**, **Palette**, **Layers**, **Package**, **Export** or
   **Recovery**.
3. Edit the image, then choose **Save Project** to keep a layered `.vtexture`
   file.

- **Paint**: **Pencil**, **Brush** (square or round, **Replace RGBA** or
  **Blend over**), **Eraser**, **Line**, **Rectangle**, **Ellipse**, **Fill**
  with a tolerance, and **Eyedropper**. **Wrap painting** lets strokes cross the
  edges, and **Tile Preview** shows a 3 × 3 repeat so seams stand out.
- **Transform**: crop, resample, rotate and flip the canvas, **Offset and Wrap**
  or **Offset Half Size** to bring edge seams into view, and
  **Set Canvas Size** to pad or crop without resampling.
- **Selection**: a rectangle selection limits every edit; copy, cut, paste,
  move, resize or rotate the selected pixels.
- **Palette**: choose the **Game palette**, then **Remap to Palette**
  (optionally dithered) or **Refresh Palette Source**. Existing pixels keep
  their colours until you remap or export.
- **Layers**: visibility, locks, opacity, blend modes, **Import Layer…**,
  **Merge Down** and **Flatten**.

On the canvas, arrow keys move the pixel cursor, <kbd>Space</kbd> applies the
tool or sets a shape's end points, <kbd>Shift</kbd>+arrow keys grow a
selection, <kbd>F</kbd> fits, <kbd>0</kbd> shows actual size and
<kbd>Esc</kbd> cancels. Middle-drag pans. Each stroke is one undo step.

The **Recovery** section keeps background copies of unsaved work; restoring
one opens it as an unsaved draft. Images are 8-bit RGBA or indexed colour, up
to 4,194,304 pixels (for example 2048 × 2048) and 32 layers. Pressure-sensitive
painting is not supported.

### Export and convert

**Export PNG** on the page header writes the image on screen as a separate PNG
file. For game formats, use the editor's **Export** section:

1. Choose an export profile.
2. Choose **Preview and Validate**. The preview shows the encoded colours, the
   palette's source, alpha changes, engine warnings and every mip level.
3. Choose **Export…** and pick where to write the file.

<details>
<summary>Export profiles</summary>

| Profile | CLI name | Notes |
| --- | --- | --- |
| **PNG · RGBA** | `png` | Full colour and alpha. The target engine must read PNG. |
| **PNG · game palette** | `png-indexed` | Palette indices with an embedded palette. |
| **Targa · RGBA** | `tga` | 32-bit with alpha; suits Quake III. |
| **DDS · BGRA8** | `dds` | Uncompressed, one surface, no mip levels. |
| **FTX · RGBA8** | `ftx` | 32-bit colour and alpha. |
| **PCX · opaque palette** | `pcx` | 8-bit with an embedded palette. |
| **Quake · miptexture** | `quake-miptex` | Four mip levels; sides divisible by 16. |
| **Quake · WAD2 texture** | `quake-wad2` | One miptexture in a new WAD2 file. |
| **Quake II · WAL** | `quake2-wal` | Four mip levels with flags and animation name; index 255 is reserved. |
| **Doom · flat** | `doom-flat` | Exactly 64 × 64 pixels. |
| **Doom · patch** | `doom-patch` | Columns with transparency and offsets. |

</details>

Indexed profiles need a real palette; a generated stand-in is used only if you
tick **Allow generated palette**. Choose how transparency is handled with
**Preserve or reject**, **Composite on matte** or **Binary threshold**, and
tick **Use source-port limits** to relax the classic size limits, with
warnings. Exporting never marks the project as saved. To convert many package
images at once, use `asset convert` on the command line.

### Stage a texture into a package or map

1. With the target package open, enter the **Package path** in the editor's
   **Package** section: a path with the profile's extension, such as
   `textures/base/rust.tga`, or a bare lump name for a WAD.
2. Tick **Replace existing entry** if the package already has that file.
3. Choose **Stage Export** to add the texture to the package's staged changes,
   or **Stage and Apply** to also put it on the brushes and patches selected in
   **Levels**.

Quake miptextures go into a WAD2 under the same name as the export. Doom flats
and patches go between `F_START`/`F_END` or `P_START`/`P_END` markers, which
are added if missing. **Stage and Apply** works with PNG or TGA files under
`textures/` in Quake III maps, and with Quake II WAL files or WAD2 miptextures
in Quake-family maps.

Staged textures appear in the browser straight away, but nothing is written
until you save the package in **Packages** and the map in **Levels**,
separately (see [Packages](manual/packages.md)). For Quake, point the map's `wad` key
or the compiler's WAD search path at the saved WAD2. WAD3 output and Doom
`PNAMES`/`TEXTURE1` wall definitions are not available, so a staged Doom patch
still needs a wall definition before a level can use it.

### Work with sprites

- Quake and Half-Life `.spr` sprites, including frame groups, and Quake II
  `.sp2` sprites decode in the browser; choose a frame with **Sprite frame**.
  A `.sp2` file names images stored elsewhere in the package, and up to 256 of
  its frames are loaded. Doom sprite lumps use the patch format and decode like
  patches.
- **Edit Selected** opens the frame on screen in the Texture Editor as an
  ordinary image, which you can export with any profile, such as a Doom patch.
- The **Sprite Creator** tab plans a new sprite: choose the **Engine** (Doom or
  Quake), **Name**, **Frames** and **Rotations**, then **Plan Sprite** to list
  the frame names, palette and package staging lines. It writes no files.

Planned: writing `.spr` and `.sp2` files, staging frames into a WAD's sprite
namespace, and animating sprite frames in the editor.

### Generate a texture (optional)

**Generate** on the page header opens the Texture Generator. It draws tiling
variants in the game's format and palette with an image model you have set up,
or makes one from a picture of your own with no AI at all. AI is off by
default; see [AI assistant](manual/ai.md#generate-levels-textures-and-sounds).

### Command-line equivalents

The `texture`, `asset` and `sprite` command families use the same services as
the page. For example:

```sh
vibestudio --cli texture palette ./mymod/pak0.pak --palette quake --json
vibestudio --cli texture export ./wall.vtexture --profile tga --output ./wall.tga --dry-run --json
vibestudio --cli asset convert ./pak0.pk3 --entry textures/base/wall.png --output ./converted --format png --resize 128x128 --dry-run
```

<details>
<summary>All texture and sprite commands</summary>

| Task | Command |
| --- | --- |
| Decode an image or sprite to PNG | `texture decode` |
| Report which palette decodes indexed art | `texture palette` |
| Create, edit or check a `.vtexture` project | `texture create`, `texture edit`, `texture inspect` |
| List profiles, check an export, or export | `texture profiles`, `texture validate`, `texture export` |
| Stage into a package draft | `texture stage` |
| List or restore recovery copies | `texture recoveries`, `texture recover` |
| Generate a texture, or its source-port companion maps | `texture generate`, `texture derive` |
| Inspect or bulk-convert package images | `asset inspect`, `asset convert` |
| List the formats this build reads | `asset formats --module textures` |
| Plan a sprite | `sprite plan` |
| Put a texture on map objects | `map apply-texture` |

</details>

See [Command line](manual/cli.md) for options and exit codes.

### Learn more

- [Texture Editor reference](TEXTURE_EDITOR.md): every tool, limit and export rule.
- [Asset formats](ASSET_FORMATS.md) and the [support matrix](SUPPORT_MATRIX.md): exact format variants and limits.
- [AI automation](AI_AUTOMATION.md): how the Texture Generator works.

## Materials and shaders

The **Materials** page shows every texture, shader and material in a package
the way its game draws it, animated in real time, and lets you edit each one
as text or as a node graph. It covers Doom wall textures and flats, Quake WAD
textures, Quake II WAL textures, Quake III shaders and Doom 3 materials.

> [!NOTE]
> **Status: Partial.** Browsing, the live preview, checking, text and node
> editing and saving work and have automated tests on generated files, but
> they have not been proven on real game packages yet. The preview is drawn
> by VibeStudio itself and approximates each engine: see
> [What the preview leaves out](manual/materials.md#what-the-preview-leaves-out).

### Open materials

Choose **Materials** on the rail, or press <kbd>Ctrl</kbd>+<kbd>8</kbd>
(<kbd>Cmd</kbd> on macOS), with a package open:

- **File** > **Open Package…** opens a PAK, WAD, ZIP or PK3 file, and
  **File** > **Open Folder Package…** a game or mod folder.
- **Open Script** on the page, or dropping a `.shader` or `.mtr` file on the
  window, opens a loose script. Its images and Doom 3 tables come from the
  open package, so open the game's or mod's package first.

You can also jump here from other pages: right-click an image on
**Textures**, or a texture in the **Levels** texture list, and choose **Show
in Materials**. From **Textures** the library lists the materials that read
the image; from **Levels** it selects the material the map uses (Quake III
maps leave out the `textures/` folder, which is added for you).

The page reads the package the first time you show it, with progress in the
strip at the top. The library on the left shows each material as a swatch:
a triangle marks animated materials, and an exclamation mark ones the game
would refuse to draw. **Show as List** switches to a compact list, and
**Animate Swatches** plays animated swatches while they are on screen.

Type in the filter to search names. Terms test what the material is, as in
the command line's `--where`:

| Term | Keeps |
| --- | --- |
| `animated=yes` | Materials that change over time |
| `engine=doom3` | One engine's materials (`doom`, `quake`, `quake2`, `quake3`, `doom3`) |
| `kind=implicit` | Quake III and Doom 3 images no script names, which the game draws with its default material |
| `errors>0`, `rejected=yes` | Materials with problems, or ones the game would refuse |
| `surfaceparm=nolightmap`, `sky=yes` | Surface parameters and traits |
| `shadowed=yes` | Definitions another one with the same name overrides |

### Preview a material

Choose a material to see it drawn by its engine's rules: Quake III stages,
blending, waves, scrolling and deforms; Doom 3 lighting with a moving light;
Doom sector light and the colormap; Quake and Quake II liquids, skies and
animations. A material that animates plays by itself, unless **Reduce
motion** is on in Accessibility settings.

- **Play** and **Pause** (<kbd>Space</kbd> in the preview), **Restart**, the
  speed menu and the time bar control the clock. <kbd>,</kbd> and
  <kbd>.</kbd> step a tenth of a second.
- The shape menu draws the material on a wall, floor, cube, sphere,
  cylinder or the inside of a room (used for skies and fog).
- Drag to turn the shape, Shift+drag (or drag with the right button) to move
  the light, and scroll to zoom. The arrow keys, <kbd>+</kbd>, <kbd>-</kbd>
  and <kbd>Home</kbd> do the same from the keyboard; double-click resets.
- **Engine View** holds the game's own drawing choices: overbright and
  lightmap brightness for Quake III, sector light, distance shading, fake
  contrast and Boom wrapping for Doom, the renderer and light style for
  Quake, intensity for Quake II, and the interaction style, light and
  ambient light for Doom 3.

When the game would refuse a material, the preview shows what it draws
instead (Quake III's default shader, for example) and says why under the
picture. Turn off **Show What the Engine Draws Instead** to see the stages
anyway.

The preview and the swatches draw with your graphics card, through the 3D
renderer chosen in **Settings** > **Appearance and Language** > **3D
Rendering**. If neither OpenGL nor Vulkan works, the preview says why; see
[3D views stay empty](manual/troubleshooting.md#3d-views-stay-empty).

### Edit as nodes

The **Nodes** tab shows the material as a graph: images and coordinate,
colour and geometry nodes feed stages, which feed the material in draw order.
Select a node to edit its properties beside the graph.

- **Add Node** adds a stage, a coordinate change, a colour or alpha
  generator, a deform or a surface setting. Some need a stage: select the
  stage, or a node inside it, first.
- <kbd>Delete</kbd> removes the selected node, and
  <kbd>Alt</kbd>+<kbd>Up</kbd> or <kbd>Alt</kbd>+<kbd>Down</kbd> moves a stage
  or texture modifier earlier or later.
- The arrow keys follow the wires from node to node; <kbd>Enter</kbd> moves to
  the node's properties. Right-click for more, such as wrapping a Doom 3
  expression in an operator or expanding a `diffusemap` shorthand into a
  full stage.

Every node edit is a change to the text, so <kbd>Ctrl</kbd>+<kbd>Z</kbd>
undoes it from either tab.

### Edit as text

The **Text** tab shows the whole script with highlighting. As you type, the
preview, the graph, **Images** and **Problems** follow, and problems are
underlined where they are. For games without material scripts the text is
the form their tools use:

| Game | Text | Saved as |
| --- | --- | --- |
| Doom (Boom) | Animation and switch tables in SWANTBLS form | `ANIMATED` and `SWITCHES` lumps |
| Doom (ZDoom ports) | `ANIMDEFS` | The `ANIMDEFS` lump or file |
| Quake II | The WAL header as `.wal_json` | The WAL's header, pixels untouched |
| Quake | None: the name sets animation (`+0`), liquids (`*`) and skies | Read only |

**New Material** starts a Quake III shader or Doom 3 material from a
template, such as a pulsing glow, rippling liquid, sky or flickering light.

### Check and save

**Problems** lists what the game would warn about or refuse, with the line;
**Images** lists every image the material reads and where it was found; and
**Details** sums the material up with the other materials that share its
images.

**Save** (<kbd>Ctrl</kbd>+<kbd>S</kbd> on the page) writes a loose script in
place. For a script, lump or WAL inside the open package it stages the
change, which you review and save with the package on the **Packages** page.
**Revert** goes back to the saved text; Undo brings your edits back.

### What the preview leaves out

- It draws a single shape without a map: no real lightmaps, no bump-mapped
  map geometry, and Doom 3 materials only with the default interaction.
- RoQ and CIN videos show a moving placeholder.
- The rules follow Quake III Arena (ioquake3). Enemy Territory's
  `implicitMap`, `implicitMask` and `implicitBlend` are drawn as that game
  draws them; other Return to Castle Wolfenstein, Enemy Territory and Jedi
  Knight keywords are read with a warning but not drawn.
- A Doom PWAD's textures are not merged with its IWAD's.

### Command-line equivalents

```sh
vibestudio --cli material list ./baseq3 --where "animated=yes" --json
vibestudio --cli material validate ./mymod --base ./baseq3
vibestudio --cli material render ./baseq3 --material textures/sfx/fire_ctfblue --frames 8 --fps 10 --output fire.png
```

`material render` draws with the same renderer; add `--renderer opengl` or
`--renderer vulkan` to choose one for that run.

<details>
<summary>All material commands</summary>

| Task | Command |
| --- | --- |
| List the materials in a package or script | `material list` |
| Show one material, its images and problems | `material inspect` |
| Check materials the way the game loads them | `material validate` |
| Draw a material, or an animation sheet, to PNG | `material render` |
| Print the node graph, or apply node edits | `material graph` |
| Apply text edits to a script | `material edit` |
| List templates, start a material from one | `material templates`, `material new` |
| Compile Doom animation and switch tables | `material doom-tables` |
| Show or rewrite a Quake II WAL header | `material wal` |

</details>

See the [command-line reference](manual/cli.md) for the shared options and exit
codes.

## Models

The **Models** page previews the models in a package and leads to three
editors: the **Mesh Editor** for geometry, UVs, frames, tags and skeletal
models, **Design Prop** for building simple props from primitives, and
**Assemble** for linking models at their tags.

These editors open as tabs within **Models**. Use **Browser** to return to the
asset list; switching tabs or studio pages keeps your current work open.

> [!NOTE]
> **Status: Partial.** Models from Doom source ports to the Doom 3 family
> open and preview, and MDL, MD2, MD3, MD5, IQM, ASE and OBJ can be edited and
> exported. Everything has automated tests, but the decoders have only been
> tested against files built by those tests, not real game files, and the
> modeller's [release checklist](MODELLER_RELEASE.md) is not met yet.

### Preview models

Choose **Models** on the rail, or press <kbd>Ctrl</kbd>+<kbd>3</kbd>
(<kbd>Cmd</kbd> on macOS). The **Models** sidebar lists the models in the open
package, so open a package or a folder first (see [Packages](manual/packages.md));
**Go to File** (<kbd>Ctrl</kbd>+<kbd>P</kbd>) opens a loose model's folder and
selects it. The filter accepts words and terms such as `ext=md5mesh size>64kb`.

Select a model to load it in the background; **Cancel Preview** stops a slow
one. The viewport draws with your graphics card through OpenGL or Vulkan
(**Settings** > **Appearance and Language** > **3D Rendering**; see
[3D views stay empty](manual/troubleshooting.md#3d-views-stay-empty) if it says it
cannot draw):

- Choose **Textured**, **Flat shaded** or **Wireframe**, and tick **Grid**,
  **Axes**, **Edges** or **Cull backfaces** (turn culling off for single-sided
  models such as flags).
- The view orbits, pans and zooms with the controls of your modeller profile
  (see [Choose your controls](manual/models.md#choose-your-controls)); hover over the view to
  see them. With the viewport focused, arrow keys orbit, <kbd>Shift</kbd>+arrow
  keys pan, <kbd>+</kbd> and <kbd>-</kbd> zoom, <kbd>Home</kbd> frames the model
  and <kbd>0</kbd> resets the view.
- Pick an animation from the list, then **Play** or <kbd>Space</kbd>.
  <kbd>Page Up</kbd> and <kbd>Page Down</kbd> step through frames, and the fps
  box sets the preview speed without changing the file.

The sidebar on the other side has four pages: **Summary** lists frames,
surfaces, tags, vertex and triangle counts, animations and skin paths;
**Skin** changes the preview's materials; **Metadata** shows header fields and
raw details; and **Skeleton** lists the joints, skeletal clips and companion
files of skeletal models.

### Supported formats

| Games | Formats | Edit and write |
| --- | --- | --- |
| Doom source ports (GZDoom, Zandronum, Eternity) | KVX voxels, plus MD2 and MD3 through MODELDEF | MD2, MD3 |
| Quake, Hexen II, Quake II, Heretic II | Quake MDL, Hexen II MDL, MD2, Heretic II FM | MDL, MD2 |
| Half-Life and its mods | Half-Life MDL, with its texture and sequence files | Read only |
| Quake III Arena, Team Arena, Elite Force, Darkplaces | MD3, MDR, IQM | MD3, IQM |
| Return to Castle Wolfenstein, Enemy Territory | MDC, MDS, MDM with its MDX | Export as MD3, which both games load |
| Jedi Outcast, Jedi Academy, Soldier of Fortune II | Ghoul 2 GLM with its GLA | Export as MD3 |
| Doom 3, Quake 4, Prey, Quake Wars, The Dark Mod | MD5 mesh and animation, LightWave LWO, ASE | MD5 mesh, MD5 animation, ASE |
| Any modeller | Wavefront OBJ | OBJ (one frame) |

Some models need other files to be complete. VibeStudio looks for them in the
same package, or in the folder of a loose file and the folders above it:

- An MDM needs its MDX, and a GLM its GLA. Without them the model cannot be
  posed and does not open; the message says where it looked.
- An MD5 mesh picks up every `.md5anim` beside it, and the animations a Doom 3
  `.def` file declares for it, under their declared names.
- A Half-Life model reads `<name>T.mdl` for textures and `<name>01.mdl` onwards
  for animation. Missing ones are named in a warning.

Jedi Outcast player models on the Jedi Academy skeleton are refused:
Jedi Academy remaps them with a table VibeStudio cannot include for licensing
reasons, so open them with the Jedi Outcast `_humanoid.gla`.

<details>
<summary>Every format, with what it keeps</summary>

Run `vibestudio --cli model formats` for the same list with its notes. The
[native formats record](MODEL_FORMATS.md) and the
[support matrix](SUPPORT_MATRIX.md) have the detail.

| Format | Extension | Kept on reading |
| --- | --- | --- |
| Quake MDL | `.mdl` | Every pose, frame groups and timing, indexed skins and skin groups, header fields |
| Hexen II MDL | `.mdl` | Poses, frame groups, skins, separate texture coordinates, model flags |
| Quake II MD2 | `.md2` | Every pose, skin names and skin size |
| Heretic II FM | `.fm` | Frames, skins, mesh nodes as surfaces |
| Quake III MD3 | `.md3` | Surfaces, shaders, every frame and tag |
| MDC | `.mdc` | Base and compressed frames, tags, shaders |
| MDS | `.mds` | Bones, weights, frames, tags |
| MDM and MDX | `.mdm`, `.mdx` | Surfaces and weights; bones and frames |
| MDR | `.mdr` | Bone matrices for every frame, the first level of detail, tags |
| Ghoul 2 | `.glm`, `.gla` | Surfaces and their hierarchy, bolts as tags, weights; the skeleton and frames |
| Inter-Quake Model | `.iqm` | Meshes, joints, weights, animations |
| MD5 | `.md5mesh`, `.md5anim` | Joints, meshes, weights, shaders; hierarchy, bounds and frames |
| LightWave | `.lwo` | The first layer: points, polygons, UV maps, surface names |
| ASCII Scene Export | `.ase` | Objects, materials, mapping |
| Half-Life MDL | `.mdl` | Bones, sequences, body parts, embedded textures, attachments |
| KVX | `.kvx` | The first mip level, as coloured faces |
| Wavefront OBJ | `.obj` | Polygons, UVs, normals, smoothing groups, `usemtl` paths |

</details>

### Skins in the preview

Skins come from the open package: the path the model names, then the same name
with a `.pcx`, `.tga`, `.jpg`, `.png`, `.wal` or `.lmp` extension. MDL,
Half-Life and KVX models carry their own skins or colours. Indexed skins use
the palette chosen on the **Textures** page (see
[Textures and sprites](manual/textures.md#choose-a-palette)).

The **Skin** page changes the preview only: choose a **Preview surface**,
**Material slot**, **MDL skin** or **Skin member**, apply a package `.skin`
file with **Skin File…**, or **Reset**. To change what a model really uses,
edit its materials in the Mesh Editor.

### Skeletal models

MD5, IQM, MDS, MDM, MDR, Ghoul 2 and Half-Life models animate through joints.
VibeStudio poses every skeletal animation into ordinary frames, so you can
preview, edit and export them like any other model, and the skeleton is kept
alongside:

- The **Skeleton** page lists the joints as a tree and the skeletal clips.
- Edit the bind pose and the vertices you move are re-bound to their joints.
  The bind pose is the frame named `bindpose`, or the first frame when a
  format's first frame is already the bind pose; edits in other frames are not
  re-bound.
- Mesh, UV, surface and frame edits keep each vertex's weights from the vertex
  it came from, never from where it ends up:
  - Cuts such as **Loop Cut**, **Subdivide** and **Bisect** interpolate the
    weights of the edge they split.
  - Copies made by **Extrude**, **Inset** or **Solidify** keep their source's
    joints, even next to another part, and turn with them.
  - **Merge** and **Weld by Distance** give merged vertices the equal blend of
    their weights; with **Preserve Seams** on, differently weighted vertices
    are not welded.
  - Separated or joined surfaces keep their own weights.
  - **Symmetrize** asks for each left and right joint pair (**Detect Pairs**
    fills them from the joint names).
  - **Add Primitive** on a skinned surface asks how the new vertices follow the
    skeleton: rigidly to a joint or to the nearest bones (optionally as a
    surface of its own), or as a new unskinned surface. **Repeat Last** asks
    again.
  - Interpolated vertices can end up with more joints than the surface had;
    the status line counts them, and **Reduce and normalise weights** limits
    them before exporting to a format with a lower limit.

  The status line names interpolated, mirrored, rebound, merged and bound
  weights. An edit that cannot say where a skinned vertex came from is refused.
  See [Skin weights through mesh edits](MODEL_SKIN_PROVENANCE.md) and the
  [tool-by-tool table](MODEL_SKINNING.md#weights-through-other-tools).
- While a surface is skinned, **Duplicate Frame**, **Delete Frame** and **Copy
  Full Pose** are disabled on the bind frame and on frames baked from a
  skeletal clip, and **Insert In-between Frames** between two such frames;
  their tooltip says why. Use **Skeleton** > **Edit Skeletal Animation** to
  resize, copy or interpolate clip poses instead. On frames you edited
  independently they work as usual, and the other frames keep their clips.
  Once every surface is unbound they work on every frame.
- **Save** keeps the skeleton in the `.mesh.json` source.
- Export as MD5 or IQM to keep the joints, or as MD3, MD2 or MDL to bake the
  animation into frames.

The Mesh Editor holds at most 1,024 frames and 1,048,576 frame vertices
(vertices times frames). A larger skeletal model opens with a **working set**:
the clips that fit, in file order, baked into editable frames. This applies
whether you open the file with **Open / Import…** or choose **Mesh Editor** for
a model selected in **Models**. The status line says what was baked and what was
left out, and **Skeleton** shows the working set with a budget meter. The
skeleton keeps every clip, and MD5 and IQM exports write them all; MDL, MD2,
MD3 and OBJ exports name the clips they leave out.

To change it, open **Skeleton** > **Working Set…**, check the clips to bake and
watch the meter, then choose **Apply** or press Enter; **Fit Automatically**
and **Bind Pose Only** set the checklist for you, and **Show Details** lists the
full report. A clip too long to bake cannot be checked.
It is one undoable edit, and a clip whose frames you edited on their own stays
until those edits are undone. **Two-Bone Inverse Kinematics**, **Animation
Curves**, **Edit Animation Keys** and **Transfer Animation Range** list clips
outside the working set too and preview them one sample at a time. See
[the skeletal working set](MODEL_SKELETAL_WORKING_SET.md).

#### Edit joints and weights

Open **Skeleton** > **Edit Skeleton** in the Mesh Editor. Add or rename joints,
reparent them while preserving poses, or delete an unused joint without children
or attachments. The first joint binds every vertex of a single-frame model.
Existing vertex animation is not converted implicitly.

To assign weights, select vertices, edges or faces on one skinned surface,
choose **Assign vertex weight**, then choose a joint and a weight from zero
to one. Other influences share the remainder proportionally. Zero removes this
influence; one assigns the vertices entirely to the joint. **Apply** validates
one undoable edit and updates the animation preview.

Weight edits protect independent frame/normal edits and require a retained bind
pose. Validation explains unsupported imported offsets, missing skinning or
changed clip ranges. Save keeps joints/weights in `.mesh.json`; MD5/IQM exports
retain them and MDL/MD2/MD3 exports use the baked frames. Bone overlays and
viewport joint handles and [staged weight painting](MODEL_WEIGHT_PAINT.md) are available.
See [Skeleton authoring](MODEL_RIG.md)
for precise limits and `model rig` CLI examples.

#### Inspect weights and bind surfaces

Open **Skeleton** > **Skin Weights and Binding…**. **Inspect weights** shows
each vertex's joint weights, sum and issue labels. Choose a weight range or
issue filter and **Select vertices** to return those components to the editor.

For an edit, choose **Bind vertices**, **Rebind existing weights**, **Reduce and
normalise weights** or **Unbind surfaces**, then choose **Review**. Check the
proposed deformation at different preview frames and inspect the **Weights**
table. **Apply** commits one undo step; changed options require another review.

Binding supports one rigid joint or distance weights from nearby joint origins
or bones. First binding and unbinding require complete surfaces. Replacing
existing weights requires its checkbox. Independent frame/normal edits are
protected; attachments and collision keep their own tracks. Rebinding explicitly
reconstructs unusual imported offsets, so review its animation carefully.
The [binding reference](MODEL_SKINNING.md) explains limits, repairs, export
handoff and the `model skinning` CLI. [Staged weight painting](MODEL_WEIGHT_PAINT.md) adds brushes and numeric row editing.

#### Create skeletal animation

Open **Skeleton** > **Edit Skeletal Animation**. **Position bind joint** moves
a joint's rest position while keeping the model and existing animation in place.
Create a clip with **Add skeletal clip**, choose its pose count and FPS, then
use **Transform joint pose** to move or rotate a joint at the desired poses.
Indices start at zero within that clip. **Move descendant joints** keeps the
branch together; model and local axes are available.

Choose **Interpolate poses** and an inclusive endpoint range to fill the poses
between them. This blends the joint hierarchy along rotation arcs. **Copy whole
pose** copies all joints from another sample or the bind pose. Apply returns to
the editor; choose the corresponding clip in **Animation** to preview it.

The dialog also duplicates, renames, resizes and deletes clips. Extending holds
the last pose; shrinking removes the tail. Existing custom ranges, manual tags,
collision tracks and native MDL timing follow structural edits. Pose transforms
leave manual tags and collision in place; refit collision when required. Edits
that would overwrite independent geometry or attachment changes are refused.

Save preserves all joint samples. MD5 animation and IQM exports use those samples;
MDL, MD2 and MD3 use the baked preview frames. Native format limits still apply.
Viewport bone manipulation, [two-bone IK](MODEL_IK.md) and
[animation curves](MODEL_ANIMATION_CURVES.md) are available; persistent
constraints remain planned. The
[skeletal authoring reference](MODEL_RIG.md#author-skeletal-animation) includes
`model pose` automation and exact limits.

### Edit a mesh

1. Select a model and choose **Mesh Editor** on the page header. With no model
   selected it starts a new mesh; **Model > Open / Import…** in the editor
   opens any format above, or a `.mesh.json` source.
2. Select faces, vertices or edges in the view or the outliner, and edit them
   with the tools below.
3. Choose **Model > Save** (<kbd>Ctrl</kbd>+<kbd>S</kbd>) to keep an editable
   `.mesh.json` source. Game files are exported separately (see
   [Export and build game files](manual/models.md#export-and-build-game-files)).

#### Save and review output

The mesh editor checks that a loaded source still has its original contents and
resolved path before saving over it. If another application changes or removes
the file, reload it or save to another path. Saving through a hard-link alias of
the loaded source is also refused; use its original path.

CLI authoring commands use the same checks with `--dry-run`, including complete
serialisation and the 64 MiB editable-source limit. A dry run leaves files,
folders and save locks untouched. It does not reserve the destination or test
write permissions, so a subsequent write can still fail.

#### Check source size and undo history

Beside **Keep local recovery copies**, a summary gives the size of the source
against its 64 MiB limit, and how many undo steps are kept and the memory they
hold; hover over it, or read it with a screen reader, for the full account. The
**Undo** and **Redo** tooltips repeat the steps available.

- Sources stay readable JSON while they fit. A larger model, such as a long
  vertex animation or a skeletal model with many clips, saves its geometry and
  joint matrices as packed arrays (source version 12), about half the size, and
  the status line says so after **Save**, since older builds cannot read it.
  Whole-number geometry and simple joint matrices can be smaller as readable
  JSON, so such a model saves readable when only that form fits. `model import`
  and `model edit` report the size and `"encoding"` in `"source"`, and the
  skeletal commands in `"outputSource"`.
- An edit that would take the source over 64 MiB is refused and nothing changes.
  Delete frames, clips, curve keys or embedded skins, or bake fewer clips into
  the [skeletal working set](MODEL_SKELETAL_WORKING_SET.md). A model opened
  over the limit can be reduced, but it cannot be saved or recovered until it
  fits; recovery copies pause and say why.
- **Model** > **Undo History Limit…** sets the memory undo and redo may keep for
  each open model, 64 MiB by default (16–4,096 MiB); the status strip shows the
  source size and the history in use. A step costs only what it changed, so
  moving vertices in one frame costs that frame. When an edit fills history the
  oldest undo steps are discarded and the editor says so once; the step before
  the current one and every redo step are always kept, so **Undo** and **Redo**
  never lose work. A typed value applies when you press Enter or leave the
  field; a lower limit discards old steps at once and says so.

See [Editable Meshes](MODEL_MESH.md#sources-import-and-export) for the source
format and how history memory is counted.

Existing CLI outputs require `--overwrite`, except ordinary in-place collision
edits. Most authoring commands can save back to the loaded `.mesh.json` with
that flag; `model tool`, `model curve-transfer` and `model uv-density` require
separate outputs. Imports and native exports protect their sources. Stroke
recipes, joint maps, replacement curve files and knife path files are protected
even with `--overwrite`, including alternate spellings and hard-link aliases.

Package-backed imports and exports also protect their archive, source folder
contents and portable draft files through hard-link aliases. Frame and assembly
exports, player packages and skin imports use the shared package checks. If a
linked destination needs a large folder scan, validation reports progress and
can be cancelled. If source protection cannot be completed, choose a new output
outside the input package, folder or draft.

#### Find your way around

The editor is laid out like the Levels page:

- The menu bar starts with **Model**, which holds the document commands:
  **Open / Import…**, **Repair Import…**, **Recover…**, **Save**,
  **Save As…**, the exports, **Stage in Package**, **Stage and Place** and
  **Keep Local Recovery Copies**. The mesh menus follow it.
- The editing header above the view starts with **Undo** and **Redo**, then the
  select modes, **X-ray**, the shading (**Solid**, **Wireframe**, **UV
  Checker**, **Material**), the view preset and **Frame Model**, then
  proportional editing, symmetry, **Delivery targets** and command search.
  When the header is too narrow, the rest fold under the arrow at its end.
- The **outliner** sidebar lists surfaces, frames, tags, collision volumes and
  joints; its **Add** page holds primitives.
- The view sits in the middle, one view or four, with the timeline below
  it. **Customise Controls…** can hide the timeline for a profile.
- The property sidebar holds **Item**, **Tool**, **Surface**, **Animation**,
  **Skeleton**, **Collision**, **Quake MDL**, **Export**, **Health** and
  **View**. Choose a tab to open its page and choose it again to close it.
- The status strip along the bottom shows the last message, the package and
  level the model hands off to, and the recovery state, each on one line. A
  message too long for its space ends in an ellipsis; point at it, or reach it
  with <kbd>Tab</kbd>, to read the whole message. **Export Report…** appears there after an export.
  In **Material** shading the strip also shows the material images: how many
  are ready and how many have a problem, a progress bar and **Cancel** while
  they load, and an **Images** menu with **Reload Images** and **Details…**.

The layout keeps the view in front at large text sizes and on a laptop screen.
Sidebar text, buttons and check boxes wrap rather than widening the sidebar,
and a choice too long for its box shows in full in its list. When the view
would get less than about half the width, the property sidebar folds to its
tabs and the status strip says so; choose a tab to open it again. A sidebar you
open stays open: if both no longer fit beside the view, the other one folds.
The editor's dialogs open within your screen, with their buttons in view; a
dialog with more than fits scrolls.

On the **View** page, **Layout** switches between **One view** and **Four
views**, and each of the four panes can show Perspective, Top, Bottom, Front,
Back, Left or Right, named in its corner. Click a pane to make it the one you
work in, or press <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Page Down</kbd> (**Next
View**) or <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Page Up</kbd> (**Previous
View**); it gets an accent border and keyboard focus.

The navigation gizmo in the view's top corner (top right, or top left in a
right-to-left layout) shows the axes; click one to look along it. It keeps
clear of the corner text, the tool banner and **Adjust Last Operation**, grows
with the text size and hides in a view too small for it. **View > Navigation
Gizmo** turns it off.

#### Choose your controls

On the **View** page, **Controls like** sets how the editor answers the mouse
and keys:

| Profile | Navigation | Transforms | Layout |
| --- | --- | --- | --- |
| **VibeStudio (Blender-style)** | Middle-drag orbits, <kbd>Shift</kbd>+middle pans, <kbd>Ctrl</kbd>+middle zooms; <kbd>Alt</kbd>+left drag also orbits | <kbd>G</kbd>, <kbd>R</kbd>, <kbd>S</kbd> start at once | One view |
| **Blender** | As above, without the <kbd>Alt</kbd> drag | As above | One view |
| **3ds Max** | Middle-drag pans, <kbd>Alt</kbd>+middle orbits, <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+middle zooms | <kbd>Q</kbd>, <kbd>W</kbd>, <kbd>E</kbd>, <kbd>R</kbd> pick a tool, then drag | Four views: Top, Front, Left, Perspective |
| **MilkShape 3D** | Left drag rotates the 3D view, <kbd>Ctrl</kbd>+left drag pans, <kbd>Shift</kbd>+left drag zooms | <kbd>F1</kbd> to <kbd>F4</kbd> pick a tool, then drag | Four views: Front, Top, Right, Perspective |

Each profile also renames and reorders the sidebar pages the way its editor
does: Blender says **Material**, **Armature** and **Physics**; 3ds Max has
**Create**, **Modify**, **Hierarchy**, **Motion** and **Utilities**; MilkShape
3D has **Model**, **Groups**, **Materials** and **Joints**. The modeller follows
your level editor profile until you choose one here.

- **Show Every Gesture and Key…** lists the whole profile.
- **Customise Controls…** changes any key, mouse gesture, transform style or
  layout, warns when two collide, and saves only what you changed. **Reset**
  goes back to the profile.
- **Export Controls…** saves your changes as a file to share, and
  **Import Controls…** loads one.

Not every tool of those editors exists here. Each profile says what it leaves
out when you hover over it; the [modeller profiles record](MODELLER_PROFILES.md#coverage-gaps)
lists the gaps.

<details>
<summary>Selection and tool keys with the Blender-style controls</summary>

| Action | Keys |
| --- | --- |
| Whole surfaces or their components | <kbd>Tab</kbd> |
| Vertex, edge or face select mode | <kbd>1</kbd>, <kbd>2</kbd>, <kbd>3</kbd> |
| Add to or remove from the selection | <kbd>Shift</kbd>+click |
| Box or circle select | Drag on empty space or <kbd>B</kbd>; <kbd>C</kbd> |
| Edge loop, edge ring | <kbd>Alt</kbd>+click, <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+click |
| Edge loops, edge rings of the selected edges | <kbd>Alt</kbd>+<kbd>L</kbd>, <kbd>Alt</kbd>+<kbd>R</kbd> |
| Linked geometry | <kbd>L</kbd> under the pointer, <kbd>Ctrl</kbd>+<kbd>L</kbd> from the selection |
| Select all, none, invert | <kbd>A</kbd>, <kbd>Alt</kbd>+<kbd>A</kbd>, <kbd>Ctrl</kbd>+<kbd>I</kbd> |
| Hide selected, hide the rest, reveal | <kbd>H</kbd>, <kbd>Shift</kbd>+<kbd>H</kbd>, <kbd>Alt</kbd>+<kbd>H</kbd> |
| X-ray (select hidden parts) | <kbd>Alt</kbd>+<kbd>Z</kbd> |
| Move, rotate, scale | <kbd>G</kbd>, <kbd>R</kbd>, <kbd>S</kbd> |
| Extrude, inset, duplicate | <kbd>E</kbd>, <kbd>I</kbd>, <kbd>Shift</kbd>+<kbd>D</kbd> |
| Loop cut, bevel vertices, make face | <kbd>Ctrl</kbd>+<kbd>R</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>B</kbd>, <kbd>F</kbd> |
| Bevel edges | <kbd>Ctrl</kbd>+<kbd>B</kbd>; wheel or <kbd>Page Up/Down</kbd> changes segments |
| Proportional editing | <kbd>O</kbd> |
| Four views, maximise the view | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Q</kbd>, <kbd>Ctrl</kbd>+<kbd>Space</kbd> |
| Next or previous view in four views (every profile) | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Page Down</kbd>, <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Page Up</kbd> |
| Show or hide the sidebars | <kbd>N</kbd> |
| Search Mesh Editor commands | <kbd>F3</kbd> |
| Adjust Last Operation | <kbd>F9</kbd> |
| Next or previous panel (every profile) | <kbd>Ctrl</kbd>+<kbd>Tab</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> (<kbd>Control</kbd> on a Mac) |

</details>

#### Work from the keyboard

Every profile can be used without a mouse:

- <kbd>Ctrl</kbd>+<kbd>Tab</kbd> moves to the next panel and
  <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> to the previous one: the
  sidebars, the editing header, the tool shelf, the view, the timeline and
  the status strip. On a Mac, use
  <kbd>Control</kbd>+<kbd>Tab</kbd> and
  <kbd>Control</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd>, since
  <kbd>Command</kbd>+<kbd>Tab</kbd> switches applications. They are also
  **View > Next Panel** and **Previous Panel**, and no profile or
  customisation can take them. To switch between the **3D** and **UV** tabs,
  click a tab or use the arrow keys on the tab bar.
- The tool shelf is one stop: move between its buttons with the arrow keys,
  <kbd>Home</kbd> and <kbd>End</kbd>, and press <kbd>Enter</kbd> or
  <kbd>Space</kbd> to use one.
- <kbd>Alt</kbd>+<kbd>O</kbd> opens the **Model** menu (open, save, export,
  stage and recovery), and the other menus open with their underlined
  letters. <kbd>F3</kbd> also finds every **Model** menu command by name,
  **Keep Local Recovery Copies** included.
- With the VibeStudio and Blender controls, <kbd>Tab</kbd> switches between
  whole surfaces and their components, and <kbd>Shift</kbd>+<kbd>Tab</kbd>
  toggles snapping, but only while the view has focus. Everywhere else,
  <kbd>Tab</kbd> and <kbd>Shift</kbd>+<kbd>Tab</kbd> move between controls as
  usual. Use <kbd>Ctrl</kbd>+<kbd>Tab</kbd> to leave the view.
- <kbd>F9</kbd> (or **View > Adjust Last Operation** with the 3ds Max and
  MilkShape 3D controls) opens **Adjust Last Operation** with focus on its
  first setting; <kbd>Esc</kbd> takes you back to the view and leaves the
  panel open. While a setting has focus, its own keys work as in any form:
  <kbd>Space</kbd> ticks a check box and typing edits a number, even where
  your controls give those keys to a command.
- While a tool such as **Move** runs, it keeps every key until <kbd>Enter</kbd>
  confirms or <kbd>Esc</kbd> cancels it.
- Pick edges in the outliner's component table with the arrow keys
  (<kbd>Tab</kbd> leaves the table), then use
  **Select Edge Loops of Selection** (<kbd>Alt</kbd>+<kbd>L</kbd>) or **Select
  Edge Rings of Selection** (<kbd>Alt</kbd>+<kbd>R</kbd>) to grow each one; an
  edge without a loop, such as a quad diagonal, stays as it was and the banner
  says why. In vertex mode, the edges between selected vertices grow. A large
  selection shows progress and can be cancelled. MilkShape 3D keeps these in
  the **Select** menu without keys.
- **Loop Cut** (<kbd>Ctrl</kbd>+<kbd>R</kbd>) starts across the selected edge
  when exactly one edge between quads is selected. Set the cuts with
  <kbd>+</kbd>/<kbd>-</kbd>, <kbd>Page Up/Down</kbd> or a number and press
  <kbd>Enter</kbd>. A pointer over another edge takes over until it moves away.
- In four views, <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Page Down</kbd> and
  <kbd>Page Up</kbd> make the next or previous pane active, and the screen
  reader names it.
- In the **UV** view, the arrow keys pan (<kbd>Shift</kbd> for small steps) and
  <kbd>+</kbd> and <kbd>-</kbd> zoom.
- Tooltips on the header buttons and the tool shelf, the tool hints and the
  empty **Adjust Last Operation** panel name the keys your profile gives each
  command, including your own changes, or the **View** menu entry where it
  gives none.

In **Customise Controls…**, the key field records one key. <kbd>Tab</kbd> and
<kbd>Shift</kbd>+<kbd>Tab</kbd> move on and <kbd>Esc</kbd> stops recording, so
pick those keys from **Special key** when you want a command to use them.

#### Use a screen reader, high contrast or reduced motion

- The status strip's lines, the sidebar summaries and the dialogs' status
  lines read what they say to a screen reader, under their names (for
  example "Mesh editor status").
- With **Announce results to screen readers** on (Settings, **Alerts and Screen Readers**),
  the editor tells you without moving focus: exports (collision maps and
  controls included), saves, staging, a delivery target moving over its
  limits, a long operation that finished and a tool's prompt as it starts are
  announced; a refused or failed edit, export, placement or tool is announced
  at once, with its reason. A recovery copy that cannot be written is
  announced once, not at every retry. Counts after an edit, values you type
  and a tool's preview while you drag or type are not: a refused **Spin**,
  **Bevel Edges**, **Rip** or **Knife** is announced when you confirm it. With
  speech on, the same messages are read aloud: refusals as failures, a long
  operation's end with **Long tasks that finish**, and other results with
  **Status bar messages**.
- **Delivery targets** describes each format's line, the curve graph names
  the selected key, frame, value, curve and tangents, and the component
  table's **A**, **B** and **C** columns are read as the first, second and
  third vertex index.
- With a high-visibility theme, every preview, including those in the
  skeletal and animation dialogs, uses the high-contrast palette.
- With **Reduce motion** on, no preview plays: **Play / Pause** is off and its
  tooltip says why, and your play key (or **Play or Pause** in command search,
  or <kbd>Space</kbd> in a view) says why in the status strip and to the
  screen reader. <kbd>Page Up</kbd> and <kbd>Page Down</kbd> still step
  frames, and busy bars stand still and name the work instead of moving.
- Numbers in status lines, readouts, tables and the curve graph follow your
  region's format, as the number fields beside them do (in German, a vertex
  at 0.5 reads "0,5" and a clip plays at "12,5 fps"), and counts read
  "1 vertex" or "2 vertices" in each language's plural forms, in English
  until a language's own forms are translated. Copy from the component
  table, saved files and the command line keep "0.5", and so do the Quake MDL
  page's **Eye position** and **Size hint** fields, which hold the file's own
  values: type a full stop for decimals there.

While a transform runs, <kbd>X</kbd>, <kbd>Y</kbd> or <kbd>Z</kbd> constrains it
to an axis, typing a number sets an exact value, <kbd>Ctrl</kbd> snaps and
<kbd>Shift</kbd> slows the pointer. <kbd>Enter</kbd>, <kbd>Space</kbd> or a
click confirms; <kbd>Esc</kbd> or a right-click cancels and leaves the mesh
untouched. With the 3ds Max and MilkShape 3D controls, pick a tool and drag the
selection instead. Every change is one undo step. Changes to the mesh's
structure, such as extrusions and cuts, apply to every animation frame; moves
can apply to **All frames** or the **Current frame**.

The mesh menus (**View**, **Select**, **Add**, **Mesh**, **Vertex**, **Edge**,
**Face** and **UV**) hold the rest: primitives, merging, dissolving, bisecting,
symmetrising, smoothing, solidify, decimation, normals, UV projections, seams
and packing. The property pages cover the details:

- **Item** and **Tool**: exact transforms, pivots and transform axes, and the
  settings of the active tool.
- **Surface**: UVs, seams, materials, **Manage Surfaces…**,
  **Manage Material Slots…**, **Skin Variants…** (see
  [Skin variants](manual/models.md#skin-variants)) and **Apply .skin File…**.
- **Animation**: frames, named clips, attachment tags and generated in-between
  frames.
- **Skeleton**: joints and skeletal clips (see [Skeletal models](manual/models.md#skeletal-models)).
- **Collision**: static collision boxes (see [Add collision](manual/models.md#add-collision)).
- **Quake MDL**: indexed skins, frame groups, timing and header flags.
- **Export**: the package path, MD2 skin size and level origin used for export
  and staging, with **Stage in Package** and **Stage and Place** beside them.
- **Health**: reports duplicate faces, unused vertices, inconsistent winding,
  open boundaries and intersecting faces, with one-step repairs for several of
  them.

The header's **Delivery targets** button says whether the model is ready for
the game you are exporting to, format by format (see
[Delivery targets](manual/models.md#delivery-targets)), and offers **Check Delivery Targets…**,
**Decimate to MD3 Budget (1,999 Triangles)** and **Export Quake III Detail
Levels…**. **Model > Keep Local Recovery Copies** checkpoints unsaved work (the
status strip shows the last copy), and **Model > Recover…** restores a copy as
an unsaved draft.

**Edge → Edge Slide** moves selected chains or loops along surrounding quad
faces. Follow the preview rails or type a signed fraction, then confirm or
cancel. **Adjust Last Operation** also offers a distance in the reference pose.
Current/all-frame scope, seam copies, undo and CLI use the same service. UVs
stay attached to the moved vertices. See [edge sliding](MODEL_EDGE_SLIDE.md)
for rail direction, selection requirements and limits.

**Edge → Bevel Edges** (**Ctrl+B** in VibeStudio/Blender) creates chamfers or
rounded edge bands. Type a width and change segments with the wheel or
**Page Up/Down**, then confirm or cancel. **Adjust Last Operation** adds
roundness and UV tile size. Closed corners receive caps; new skin weights
follow the cut geometry across all retained poses. Original face UVs stay
separate, while new bands/caps need atlas packing where appropriate. See
[edge bevel](MODEL_EDGE_BEVEL.md) for width semantics and refusal conditions.

**Mesh → Knife** (**K** in VibeStudio/Blender) cuts a path into the active
surface. Click points, use **N** or right-click for another chain, then
**Enter/Space** to apply or **Esc** to discard. **C** toggles cut-through,
**S** restricts cuts to selected faces, **A** constrains screen angles,
**Shift** snaps midpoints and **Ctrl** ignores geometry snapping. Arrow keys
move the crosshair and **Insert** places points. The preview includes the
hover segment; **Backspace** removes a fixed point. Hidden neighbours retain
their visibility when subdivided. Every pose, UV seam and skin influence uses
the same cut interpolation. F9 adjusts visibility/selection scope. See
[knife cutting](MODEL_KNIFE.md) for projection, snapping and refusal limits.

**Face → Spin** and **Edge → Spin** make curved extrusions around the **3D
Cursor**. Type degrees, choose an axis with **X/Y/Z**, and set steps with the
wheel or **Page Up/Down**. **Face → Spin Duplicates** makes radial face copies.
**Enter/Space** confirms, **Esc** cancels and **F9** adjusts the centre, axis,
steps, closure, duplication and UV scale. Detached face profiles can close a
full turn. Review every pose, face orientation and UV packing before exporting;
see [Spin](MODEL_SPIN.md) for skinning behaviour, limits and CLI examples.

**Vertex → Rip** and **Edge → Rip** open a selected vertex or connected edge
chain; **Rip and Fill** bridges the gap. **V/Alt+V** start these in the studio
and Blender profiles. The starting pointer chooses the side; **B** swaps it
and **F** toggles filling. Type a distance, constrain with **X/Y/Z**, confirm
with **Enter**, or cancel the whole preview with **Esc**. **F9** adjusts the
offset, side face, fill and UV scale. Every retained pose is updated through
the shared service. See [Rip](MODEL_RIP.md) for rig behaviour, CLI examples
and the current unbranched-selection limits. Confirm **Rip** immediately or
enter a zero offset to split without moving; the split survives coincident
positions, other mesh/UV tools and source reopening. **Rip and Fill** requires
movement to create gap faces. **Esc** restores the original connectivity.
Keep the `.mesh.json` source: game-format derivatives generally cannot store
the version-11 connectivity metadata. **Join Surfaces** preserves separate
input islands; **Merge** or **Weld** explicitly connects compatible vertices.

**UV → Texel Density…** (also in the UV toolbar) measures existing islands
against the original texture dimensions. Choose a reference pose, inspect the
per-island results, then preview a target density for each island or for the
combined selection. Apply creates one undo step. Review overlaps and coverage
before packing or exporting; density adjustments keep island centres fixed and
affect UVs across every pose. See [texture density](MODEL_UV_DENSITY.md) for
material defaults, repair diagnostics, limits and CLI examples.

### Design a prop

**Design Prop** builds a static prop from up to 32 boxes, cylinders and planes:

1. Add parts, then set their size, position and rotation in the **Part** tab.
2. In the **Surface** tab, set each part's material path and UV scale, offset
   and rotation. **UV checker** previews tiling without project textures.
3. Choose **Save Design…** to keep the editable `.model.json` source.
4. Choose **Export MD3…** or **Export OBJ…**, **Stage in Package**, or, with a
   Quake III map open, **Stage and Place** to also add an undoable `misc_model`
   entity.

Props have one frame and no animation, tags or collision mesh. MD3 vertices must
stay within -512 to about 512 units of the origin. **Edit as Mesh** turns the
design into an editable mesh for further work.

### Assemble models at tags

**Assemble** links models through their attachment tags, each part with its own
frame range, speed and phase. Choose **Add Part**, pick the model, parent tag
and offset, then **Apply**. Save the links as an `.assembly.json` file. From
there you can bake one pose into the Mesh Editor (**Bake Pose to Mesh**),
export a pose as OBJ, MD2 or MD3 or a sampled animation as MD2 or MD3, write a
Quake III `animation.cfg` (**Native Animation…**), and review and export a
Quake III player PK3 (**Player Package…**). Assemblies link separate models;
they are not skeletal animation, and game-side animation set-up is still up to
you.

#### Open an existing Quake III player

**Open Player…** turns a player of the current package or folder into a new
assembly in one step. Choose the player and one of its complete skins, then
**Open**. The assembly gets the player's three models, with upper on lower's
`tag_torso` and head on upper's `tag_head`, the chosen skin on each part and the
player's `animation.cfg` as its native animation. **Player Package…** is filled in
with the player's ID, icon, other skins and custom sounds, so you can change a skin
or fix a tag and publish the player again. A player missing a model,
`animation.cfg` or a complete skin says why it cannot be opened.

Nothing in the package changes. The new assembly is unsaved: save it beside the
package, not inside it. To bring in only an `animation.cfg`, use **Native
Animation… > Import from Package…**. See
[Opening a Quake III player](MODEL_ASSEMBLY.md#opening-a-quake-iii-player).

#### Publish a Quake III player

**Player Package…** writes one PK3 with the lower, upper and head models, a skin
set and icon for every skin, `animation.cfg`, the materials they use and,
optionally, detail levels and custom sounds. Choose the **Player ID**, **Skin ID**,
**Head part** and icon, then:

- **Team skins (red and blue)** adds the skins team games use. Without them, team
  games show the default team model instead of yours. Give at least one part red
  and blue materials, as a stored [skin variant](manual/models.md#skin-variants) or a `.skin` file.
- **More skins** adds a row per skin. Leave a part's cell empty to use that part's
  stored skin variant of the same name, or its own materials; type a `.skin` path
  (or `package:PATH`) to use a file. An empty **Icon** reuses the package icon.
- **Detail levels** generates one or two lower-detail versions of each part, or
  uses each part's stored [detail levels](MODEL_LOD.md).
- **Custom sounds** takes a sound for any of the 13 player sounds (`death1` to
  `taunt`). Mono WAV is kept; other audio is converted to the 22,050 Hz mono WAV
  Quake III reads. Sounds you leave empty use the game's default sounds.

**Review Package** checks the package before you can export it: every skin must
cover every surface, and upper must have a `tag_weapon` for the player's weapon.
The review lists every file and notes anything the game will fill in, such as a
missing `tag_flag` or default sounds. See
[Native player packages](MODEL_ASSEMBLY.md#native-player-packages).

### Skin variants

**Skin Variants…** on the **Surface** page keeps named skins with the model, such
as `red` and `blue`. A variant gives some surfaces another material; the others
keep their own (primary) material.

1. Type a **Name** and choose **Add**.
2. Select surfaces, type a **Material** and choose **Set Material**. **Use
   Primary** puts a surface back to its own material.
3. Use **Show** to see a variant in the viewport without changing the model.
4. Choose **Apply** to keep the variants as one undo step, then save the source.

**Separate selected faces** and **Duplicate surface** give the new surface what
its origin wears in every variant, as Prepare for Target's splits do, so a split
part keeps its team colour.

**Export .skin…** writes the selected variant as a Quake III `.skin`, and
**Export Doom 3 Skin…** as a Doom 3 skin declaration. Doom 3 skins swap materials
rather than surfaces, so the review explains when a variant cannot be written that
way. Player packages use a part's variants for skins of the same name. See
[Skin variants](MODEL_SKIN_VARIANTS.md) for the rules and the
`model skin-variants` and `model skin-export` commands.

### Add collision

The Mesh Editor's **Collision** page creates static, oriented collision boxes:
add or fit a box to the selection, then move, rotate and size it.
**Export Collision Map…** writes the boxes as brushes in a separate `.map`
file, and **Place Collision in Level** adds them to the open Quake, Quake II or
Quake III map as one undoable edit. Placed brushes do not follow later edits to
the prop.

### Export and build game files

| From | How |
| --- | --- |
| Models page | **Export OBJ** writes the current frame of the selected model. |
| Mesh Editor | In the **Model** menu, **Export MD3…**, **Export MD2…**, **Export MDL…** or **Export OBJ Frame…**; **Export Other Format** for **MD5 Mesh…**, **MD5 Animation of Clip…**, **MD5 Animations of All Clips…**, **Inter-Quake Model (IQM)…** and **ASE Frame…**; **Stage in Package** or **Stage and Place** for a package and map. |
| Command line | `model build` turns a `.mesh.json` or `.model.json` source into any of those formats (`--target` names the game); `model lod` writes Quake III detail levels; `model targets` lists the delivery targets or checks a model against them. |

Each export checks the limits of its delivery target first, and reports what
the format had to leave out:

- MD3 keeps every frame and tag; Quake III Arena and ioquake3 draw at most 999
  vertices and 1,999 triangles per surface.
- MD2 keeps every frame but needs a single surface (Quake II: up to 2,048
  positions, 4,096 triangles and 512 frames), and refuses models with tags.
- MDL keeps indexed skins and frame groups on a single surface (original Quake:
  up to 1,024 vertices and 2,048 triangles; QuakeSpasm: 2,400 and 4,096), and
  also refuses tags.
- MD5 mesh and IQM keep the joints and weights; a model without joints gets a
  single origin joint (MD5) or is written as a static mesh (IQM). They animate
  through joints, so edits to frames other than the bind pose do not carry
  over; the report lists them, and MD3 keeps them.
- MD5 joints cannot scale: the md5mesh carries bind scale in its weights, and an
  md5anim refuses a pose that scales a joint differently from its bind pose.
  IQM keeps joint scale but only four influences per vertex.
- **MD5 Animation of Clip…** asks which clip to write and at what whole frame
  rate, starting at the clip the current frame belongs to, and reviews the
  result first. **MD5 Animations of All Clips…** (**Export All Clips as MD5…** on
  the **Skeleton** page) writes one `<clip>.md5anim` per clip into a folder.
  **Stage in Package** with a `.md5anim` package path opens the same chooser,
  starting at the clip the path names, such as `walk.md5anim`.
- ASE and OBJ write one frame. OBJ has no material library and leaves out
  tags.
- MDL, MD2 and MD3 have no clip table: a game rebuilds clips from the frame
  names (`stand1`, `stand2` and so on). Clips named that way come back; other
  clip names, ranges and rates are left out, and so is a skeleton.

After an export or a package handoff the status line keeps the most important
note in view, and **Export Report…** beside the recovery controls lists every
note, with what the format left out first. To refuse such exports instead, turn
on **Refuse exports that leave data out** on the **Export** page: an export,
stage or set of detail levels that would leave data out then writes nothing,
and **Export Report…** lists what it would have left out. It matches
`model build --strict` and `model lod --strict`, and lasts while the editor is
open. See
[what each export keeps](MODEL_FORMATS.md#what-each-export-keeps-changes-or-leaves-out)
for the details per format.

MD3 models can be placed in Quake III maps as `misc_model` entities; placing
other models in a level is game-specific and not automated.

#### Delivery targets

Engines and source ports accept different amounts of the same format: the
original Quake draws an MDL of 1,024 vertices, QuakeSpasm one of 2,400, Enemy
Territory an MD3 surface of 1,024 vertices where Quake III stops at 999, and
ioquake3 loads at most 128 IQM joints. Choose the game you are making the model
for, and every export, stage and set of detail levels is checked against it
before anything is written.

1. Open the **Delivery targets** button in the Mesh Editor header. It reads
   **ready**, **check** or **over** for MD3, MD2, MDL and, for skeletal models,
   IQM and MD5, with an icon for each.
2. Choose a target in **MD3 Delivery Target**, **MD2 Delivery Target**,
   **MDL Delivery Target** or **IQM Delivery Target**, and for skeletal models
   **MD5 Mesh Delivery Target** or **MD5 Animation Delivery Target** (Q2PRO's
   MD5 replacement models, for example). Each format starts at
   the target saved with the open project, else the one the game installation
   suggests (Q2PRO for `q2pro.exe`, for example), else its original engine (for
   IQM, the format's own limits). The installation is the one the project
   links, else one of the project's game, else the selected installation, as
   for releases and `model build`. The submenu's title says where the choice
   comes from.
3. Read the blockers listed under each format. Choose one to select what it is
   about: the faces past a surface's budget, the vertices outside the skin tile
   or MD3's range, or the surface. Its tooltip gives the fix. Or choose **Check
   Delivery Targets…** to check every limit across every frame and see each
   blocker with its fix and how close each count is to its limit.
4. Fix the blockers yourself, or let the studio prepare the model (below).
5. Export. An export the target refuses writes nothing; the status line says
   how many limits block it and **Export Report…** lists each with its fix.

**check** means a count taken before the writer shares seam vertices is over a
limit, or the model is too large to scan every MD3 frame as you edit; **Check
Delivery Targets…** gives the exact answer. A choice made in the editor lasts
while the editor is open; choose **Save Targets with the Project** to keep each
format's choice, and the **Refuse exports that leave data out** setting, with
the open project, where `model build` and `model lod` use them too. If the
project manifest cannot be read, nothing is saved and the status line names the
file: fix or restore it, then save again. The targets, their limits and where
each limit comes from are listed in [delivery targets](MODEL_TARGETS.md).

#### Prepare a model for its target

1. Open **Delivery targets** and choose **Prepare for MD3 · Quake III Arena…**
   (or the entry under another format with blockers).
2. Review the changes. Depending on the format they split a surface that is over
   its budget into surfaces that fit, join surfaces into the one MDL and MD2
   hold, decimate what cannot be split, move (or, when it is too large, scale)
   a model into MD3's coordinate range with its tags, and reduce the joint
   influences IQM cannot keep. Notes say what to know, such as an origin that
   moved, the skin variants that now cover a split surface's parts, or stored
   hand-made detail levels to rebuild; the end of the list says what would still
   block the target.
3. Clear a kind of change, such as **Decimate**, to leave it out; the review is
   prepared again without it.
4. Choose **Apply Changes**. Everything listed is one undo step, and the
   changed surfaces are selected. **Cancel** leaves the model as it was.

Names, skin sizes, frame counts, joints and features too small for MD3's
precision are left for you to change; the review and **Check Delivery
Targets…** list them with their fixes.

#### Detail levels

Quake III loads `name_1.md3` and `name_2.md3` beside `name.md3` and draws one
by distance; Enemy Territory also loads `name_3.md3`, and some games load none.
The source can own its detail levels, so they are reviewed, kept and staged with
the model.

1. Choose **Mesh > Clean Up > Detail Levels…**.
2. Set **Detail levels**, up to what the MD3 delivery target loads, and choose
   how each level is made: **Generated: keep a share of the triangles**,
   **Generated: reduce to a triangle count**, or **Hand-made model** with its
   file (**Browse…**). A hand-made level needs the base model's frames and
   surface names.
3. Choose **Review**. The table lists each surface's triangles per level, and
   the text below lists any problem the game would have with the set, such as
   a level with fewer frames, and the distance from which each level shows.
   **Show** displays a level in the viewport without changing the model.
4. Choose **Store in Source** to keep the levels in the source (one undo step;
   save to keep them), or **Export…** to write the base MD3 and its levels.

**Export Quake III Detail Levels…** writes the stored levels, or two halvings
when the source stores none. **Stage in Package** stages an MD3 with its stored
levels as one package step, and the level dependency review lists the levels a
package holds beside each model, so releases ship them. Every file is checked
against the delivery target before any is written; if one of the files already
exists, for example a hand-made `name_1.md3`, one question names every file
that would be replaced, and **Cancel** writes nothing. Afterwards **Export
Report…** lists each file's notes. To check a set that already exists, use
`model lod --inspect` (see [detail levels](MODEL_LOD.md)).

Writing or staging an MD3 replaces only the files written. When the folder or
package already holds detail files the game would load with the new model, such
as a `name_2.md3` from an older set, or an `.mdc` that Return to Castle
Wolfenstein and Enemy Territory load first, the status line and **Export
Report…** name them and say whether the game would read them wrongly. Write the
detail levels again, or remove those files. When the MD3 delivery target loads
fewer levels than the source stores, or none, **Stage in Package** stages the
model with the levels the target loads, and the report says which stay in the
source.

### Manipulate joints in the viewport

Choose a joint in **Skeleton**, or select **Joints** in the component table.
Use the move, rotate and scale handles; **Geometry** provides numeric values,
snapping, axes and pivots. G/R/S choose the joint handle tool. **Transform children**
moves descendants with the selected joint; turn it off to leave children in place.

The status line identifies the bind pose or the current clip/sample. Bind changes
preserve the mesh and animation; pose changes affect just the displayed sample.
Use **Edit Skeletal Animation** for a range. Previewing leaves the document
unchanged; release commits one undo step and Escape cancels. Changes to the frame,
joint or descendant option also cancel. Independently edited skin or attachment
poses are protected. Fit collision separately after posing.

**X-ray joints** reveals joints through the mesh. Turn it off to pick only visible
origins and bone segments; wireframe always reveals them. The hierarchy and table
provide keyboard access to hidden or overlapping joints. Named joint selection
survives undo, redo and recovery. See [viewport posing](MODEL_RIG.md#pose-joints-in-the-viewport)
for the shared CLI matrix option and current limitations.

### Paint skin weights

Open **Skeleton → Paint Skin Weights** after binding a surface. Choose a joint
and replace, add, subtract or smooth weights. Radius, falloff, visible/through
painting and a selected-row mask control coverage. Joint locks preserve existing
weights; an explicit remainder joint handles reducing a sole influence.

Release a stroke to validate and preview its skeletal poses. Escape cancels
pending coverage. **Undo stroke** and **Redo stroke** work inside the review
window; **Apply staged strokes** commits the batch as one editor undo step.
The weight table and **Paint rows** provide exact keyboard-accessible editing.
Cancel discards the staged batch. See [weight painting](MODEL_WEIGHT_PAINT.md)
for preservation rules, recipe replay and remaining limits.

### Transfer or mirror skin weights

Open **Skeleton → Transfer Skin Weights**. Choose a bound source and a destination
surface, nearest triangle or vertex matching, and a distance limit. For symmetry,
choose a mirror axis and direction; **Joint mapping** pairs exact names or existing
suffix pairs. Destination locks preserve chosen influences. Normal filtering
helps distinguish hard-normal seams and nearby layers; conflicting matches are
refused.

**Review** prepares the candidate without changing the source. Compare original
and resulting poses in **Preview**, inspect exact before/after **Weights**, then
**Apply** one undoable edit. Component selection can restrict an already bound
destination; an unbound destination needs a match for every vertex at full
strength. See [weight transfer](MODEL_WEIGHT_TRANSFER.md) for preservation,
mirroring, joint maps, export handoff and CLI options.

### Place a hand or foot

Open **Skeleton → Two-Bone Inverse Kinematics**. Choose a clip, an end joint
and an inclusive sample range. Its parent and grandparent form the chain.
Set the target in model units and a pole point on the desired elbow/knee side.
**Require target** refuses unreachable poses; **Nearest reachable** clamps
without stretching and reports the remaining distance. Choose whether the
end keeps its orientation or follows the lower bone.

**Review** prepares a candidate in a cancellable worker. Compare before and
after samples in **Preview**, inspect numeric **Results**, then **Apply** one
undoable edit. Changed settings require another review. IK writes dense poses;
independent affected mesh/tag edits are protected. Refit collision and refresh
exports explicitly. See [inverse kinematics](MODEL_IK.md) for exact policies,
CLI use and remaining constraints.

### Edit animation curves

Open **Skeleton → Animation Curves**, choose a clip and joint, then **Capture
Sampled Motion**. Each joint has translation, unwrapped rotation and positive
scale channels. The **Keys** table and numeric fields insert, move and remove
keys and set step, hold-next, linear or cubic interpolation with automatic, clamped, free
or flat tangents. The **Curve** graph offers the same edits with keys and handles.

**Undo draft**, **Redo draft** and **Discard draft** act locally; the 256 newest
drafts are kept. **Review**
samples the curves and validates the resulting skin and attachments. Compare
original/result motion in **Preview**, then **Apply** one document undo step.
Save uses source schema 10 for hold-next curves, or schema 9 for other curves;
native game exports use dense samples.
To return to pose or IK tools, explicitly **Stage Bake Joint Curves**, review
and apply: motion remains, and undo restores the editable keys. See
[animation curves](MODEL_ANIMATION_CURVES.md) for controls, limits and CLI use.

For groups of keys, open **Skeleton → Edit Animation Keys**. Filter joints and
channels, select rows or an inclusive sample range, then choose retime, duplicate,
remove or reduce. Retiming changes sample times around a pivot and refuses key
collisions; reduction checks continuous channel error, including between samples.
**Review** reports key counts and changes, and previews the resulting motion.
**Apply** commits the whole selection as one undo step. See
[key groups](MODEL_ANIMATION_CURVES.md#edit-groups-of-keys) for tangent,
rounding, selection and tolerance rules, and the `model curve-keys` CLI.

To reuse or reverse motion, open **Skeleton → Transfer Animation Range**.
Choose source/destination clips, inclusive source bounds, destination start,
time scale and optional **Reverse source range** or **Match clip rates**.
Select joints/channels on **Tracks**, then **Review** the counts and whole-channel
changes. Inspect original/result destination poses, including adjoining
transitions, before **Apply**. The destination interval is replaced as one undo
step. Stored curves keep their shape; unkeyed motion is captured automatically.
Every mapped key must land on an integer sample. See
[range transfers](MODEL_ANIMATION_CURVES.md#transfer-or-reverse-a-range)
for boundary behaviour and the `model curve-transfer` CLI.

### Known limits

- No model format has been tested against real game files in bulk, and no
  MD5, IQM or ASE export has been loaded in the original games yet.
- MDC, MDS, MDM, MDR, Ghoul 2, Half-Life, Hexen II, Heretic II, LightWave and
  KVX models are read only; export them as MD3 or another written format.
- Long-chain IK, persistent constraints, animation layers, retargeting and
  joint modal pointer-following remain planned. Two-bone IK, bone overlays,
  shared viewport handles and numeric joint,
  weight and skeletal pose tools are available.
  Game animation scripts (Jedi Academy `animation.cfg`,
  Wolfenstein scripts, GZDoom MODELDEF) are not read.
- A skeletal clip too long for the editable frame budget on its own cannot
  join the working set: edit it through the skeletal tools, which preview one
  sample at a time. See [the working set limits](MODEL_SKELETAL_WORKING_SET.md#limits).
- On skeletal models, merged weights are an equal blend rather than weighted by
  distance, position tools make the frames they edit independent of the
  skeleton, and an unskinned surface must be bound before it can join a skinned
  one. See [Skin weights through mesh edits](MODEL_SKIN_PROVENANCE.md#limits).
- Modifier stacks and branched rip
  junctions remain planned. [Spin](MODEL_SPIN.md) and [Rip](MODEL_RIP.md)
  provide bounded shared GUI/CLI workflows. [Knife](MODEL_KNIFE.md)
  cuts projected paths; [Bevel Edges](MODEL_EDGE_BEVEL.md) provides chamfers
  and rounded bands with joined corners. Both share GUI/CLI validation.
- The [modeller release checklist](MODELLER_RELEASE.md) lists what is still
  open.

### Command-line equivalents

```sh
vibestudio --cli model inspect ./id1/pak0.pak progs/player.mdl --json
vibestudio --cli model import ./prop.obj --output ./prop.mesh.json --dry-run --json
vibestudio --cli model build ./prop.mesh.json --output ./build/prop.md5mesh --dry-run --json
vibestudio --cli model build ./monster.mesh.json --all-clips --output-dir ./build/anims --strict --json
vibestudio --cli model controls --profile 3ds-max --section keys
```

<details>
<summary>All model commands</summary>

| Task | Command |
| --- | --- |
| Inspect a model, or export one frame as OBJ | `model inspect`, `model export` |
| List the formats the studio reads and writes | `model formats` |
| Import a model or design as an editable mesh | `model import` |
| Edit, select or run a mesh tool | `model edit`, `model select`, `model tool` |
| Check topology, UVs, intersections, or repair an import | `model topology`, `model uv`, `model intersections`, `model repair-import` |
| Surfaces, material slots, skins and appearances | `model surfaces`, `model slots`, `model skin`, `model materials` |
| Named skin variants and their `.skin` or Doom 3 skin files | `model skin-variants`, `model skin-export` |
| Tags, clips and collision | `model tags`, `model animations`, `model collision` |
| Joints, weights, bind poses, skeletal clips and the working set | `model rig`, `model pose`, `model skinning`, `model weight-paint`, `model weight-transfer`, `model ik` |
| Quake MDL skins and timing | `model mdl` |
| Editable skeletal keys, tangents and baking | `model curves` |
| Retime, duplicate, remove or reduce selected skeletal keys | `model curve-keys` |
| Assemblies, and opening or publishing a Quake III player | `model assembly` |
| Build game files or detail levels | `model build`, `model lod`, `model targets`, `model prepare` |
| Controls profiles, and print, check, share or reset controls | `model profiles`, `model controls` |
| Recovery copies | `model recoveries`, `model recover` |
| Place an MD3 in a Quake III map | `map place-model` |

</details>

See [Command line](manual/cli.md) for options and exit codes.

### Learn more

- [Mesh tools](MODEL_TOOLS.md) and [editable meshes](MODEL_MESH.md): every tool, limit and CLI option.
- [Skin weights through mesh edits](MODEL_SKIN_PROVENANCE.md): how edits keep, interpolate and mirror joint weights.
- [Native formats and skeletons](MODEL_FORMATS.md) and [modeller profiles](MODELLER_PROFILES.md).
- [Delivery targets](MODEL_TARGETS.md) and [detail levels](MODEL_LOD.md): each engine's limits, Prepare for Target and linked detail levels.
- [Model design](MODEL_DESIGN.md) and [model assemblies](MODEL_ASSEMBLY.md): props and linked models.
- [Model collision](MODEL_COLLISION.md), [material slots](MODEL_MATERIAL_SLOTS.md) and [skin variants](MODEL_SKIN_VARIANTS.md).
- [Modeller release checklist](MODELLER_RELEASE.md): what has and has not been verified.

## Audio

The **Audio** page lists the sounds in a package, draws their waveforms and
plays them. The audio editor cleans up, converts and delivers game-ready sound
effects, and multitrack sessions arrange several sounds into one.

Waveform editing and multitrack sessions open in tabs within **Audio**.
**Browser** returns to the sound list. Session mixdowns and recovery handoffs
select the receiving editor's tab in the same studio window.

> [!NOTE]
> **Status: Partial.** Browsing, waveform editing, analysis and delivery in
> each game's format work and have automated tests, but have not been proven
> in real projects. Playback needs Qt Multimedia, which Linux release builds do
> not include yet, and recording has not been tested with real audio hardware.

### Browse sounds

Choose **Audio** on the rail, or press <kbd>Ctrl</kbd>+<kbd>5</kbd>
(<kbd>Cmd</kbd> on macOS). The **Sounds** list shows the audio in the open
package, so open a package or a folder first (see [Packages](manual/packages.md));
**Go to File** (<kbd>Ctrl</kbd>+<kbd>P</kbd>) opens a loose sound's folder and
selects it. The filter takes words and terms such as `ext=wav size>1mb` or
`folder:sound/ambience`.

Select a sound to see its waveform and its **Format** tab: codec, channels,
sample rate, bit depth, bitrate and duration. **Metadata** shows the raw header
details. Compressed sounds show their header details in the browser; open them
with **Edit Sound** to see the decoded waveform.

| Format | Browse and play | Edit |
| --- | --- | --- |
| WAV: 8, 16, 24 or 32-bit PCM, 32 or 64-bit float | Yes | Yes |
| Doom digital sound lumps (DMX) | Yes | Yes |
| MP3, FLAC, Ogg Vorbis | Yes | Yes, decoded on import |
| Ogg Opus | Details; playback depends on your system | No |
| Doom PC speaker sounds, MUS, MIDI | No | No |

### Play sounds

Use **Play Sound** (<kbd>Space</kbd>), **Stop Sound** and **Loop Sound** above
the waveform, with **Volume** and a **Position** slider that also seeks within
compressed sounds.

Playback uses Qt Multimedia. Windows and macOS release builds include it; Linux
release builds do not yet, so the playback controls stay disabled there and the
**Volume** tooltip says the build has no audio playback. Everything else on
this page, including editing and export, works without it. If no output device
is available, VibeStudio reports a retryable error: choose an output in your
system's sound settings and press Play again.

### Edit a sound

1. Select a sound and choose **Edit Sound**, or choose **Open Audio…** to open a
   local WAV, DMX, MP3, FLAC or Ogg Vorbis file, a `.vsaudio` project, a
   `.vssession` session or a `.vstake` recording.
2. Drag across the waveform to select a range, or type exact start and end
   frames (the end is exclusive).
3. Apply edits, then save the project or export the result.

The editor works on a separate floating-point copy, so the source file or
package is never changed. **New…** (<kbd>Ctrl</kbd>+<kbd>N</kbd>) starts an
empty sound or a stretch of silence.

- Navigate: <kbd>Left</kbd> and <kbd>Right</kbd> move one frame and
  <kbd>Shift</kbd> extends the selection; **Zoom In**, **Zoom Out**,
  **Fit Sound**, **Fit Selection** and <kbd>Ctrl</kbd>+wheel zoom down to single
  samples.
- Edit: **Copy**, **Cut** and **Paste** move exact samples between audio
  editors (sample rate and channels must match), and **Trim**, **Delete**,
  **Silence**, **Fade In** and **Fade Out** work on the selection.
- **Effects**: **Insert Silence…**, **Mix Clipboard at Selection Start**,
  **Gain…** (-96 to +24 dB), **Normalize…** (to a peak, -1 dBFS by default),
  **Reverse**, **Convert to Mono**, **Convert Mono to Stereo**,
  **Invert Polarity**, **Remove DC Offset** and **Resample…**, which keeps
  pitch and duration.
- **Markers…** sets named cues and one loop; **Select Loop** selects it for
  playback.
- **Play Selection**, or **Play From Cursor** when nothing is selected,
  auditions your edits when playback is available; <kbd>Space</kbd> does the
  same while the waveform has focus.

Undo and redo name each edit and keep up to 32 steps. Long operations show
their progress and can be cancelled; a cancelled or failed edit leaves the
sound as it was.

### Save, recover and export

- **Save Project** (<kbd>Ctrl</kbd>+<kbd>S</kbd>) writes a `.vsaudio` project
  that keeps the exact samples, markers and selection; **Save As…** writes a
  copy.
- With **Keep local recovery copies** ticked, unsaved work is checkpointed in
  the background. **Recoveries…** in the editor, or **File** >
  **Recover Audio…**, restores a copy as an unsaved draft. Settings >
  **Getting Started** > **Audio Recovery** controls checkpoints and the offer
  at start-up.
- **Export Audio…** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>E</kbd>) writes WAV
  as 8, 16, 24 or 32-bit PCM or 32-bit float, with optional dither for integer
  output. Exporting does not mark the project as saved.
- **Export WAV** on the Audio page writes the selected package sound as a
  separate 16-bit WAV.

Export and staging also offer game presets, which mix to mono and resample a
delivery copy while your project keeps its original samples:

| Preset | Output |
| --- | --- |
| Doom | DMX lump, 11025 Hz, 8-bit mono |
| Quake | WAV, 11025 Hz, 8-bit mono |
| Quake II, Quake III | WAV, 22050 Hz, 16-bit mono |

Cues and loops are kept where the format can hold them; the export summary says
what was left out. MP3, FLAC and Ogg Vorbis output are not offered.

### Put a sound in a package or map

- **Stage Sound** adds the result to the open package's staged changes: a WAV
  in a folder, PAK, ZIP or PK3, or a DMX lump in a Doom WAD with a sound name
  such as `DSDOOR` and no extension. Review and save it from **Packages**.
- **Stage & Place in Level…**, with a Quake II or Quake III map open in
  **Levels** and a folder, PAK, ZIP or PK3 package open, converts the sound to
  22050 Hz 16-bit mono, stages it, and adds an undoable `target_speaker` after
  you review its position, looping and target name.

Staged sounds play and open from the Audio page before you save. Save the map
and the package separately (see [Level editing](manual/levels.md) and
[Packages](manual/packages.md)).

### Analyse levels and loudness

**Analyze…** measures the selection, or the whole sound when nothing is
selected, without changing it: sample peak, RMS, DC offset and full-scale
counts per channel, plus true peak and integrated loudness. Untick
**Measure loudness** to skip loudness. The figures describe the sound before
any game conversion, so analyse an exported file to check what the game will
play.

### Multitrack sessions and recording

**Multitrack…** opens a `.vssession` session for arranging mono and stereo
clips on tracks, with gain, pan, mute and solo, fades, automation, buses and
sends, up to eight effects per track, **Tempo / Meter…**, **Range…** edits,
and mixdown or **Export Stems…**. **To Session** in the waveform editor brings
the current sound into a session. Sessions have undo, saves and recovery like
the editor.

A session offers two ways to record:

- **Single Take…** records one input to a `.vstake` file.
- **Record Tracks…** records up to eight armed tracks against the session's
  backing, with punch-in, repeated loop passes and a review step for picking
  takes.

Recording starts only when you press Record, and stopping keeps what was
captured. Single takes need Qt Multimedia, so they are not available in Linux
release builds; multitrack recording needs the build's native audio support,
and the dialog says when it is unavailable. On macOS, recording needs
microphone permission. Recording has not yet been tested with real audio
hardware, and MIDI, plugins and live automation recording are planned.

### Generate sound effects (optional)

**Generate** on the page header opens the Sound Generator. Choose
**The synthesizer, on this machine**, which needs no AI and makes the same sound
for the same description and seed, or **The sound model** (ElevenLabs or a
custom endpoint) after you turn AI on. Variants are trimmed, faded, normalised
and delivered in the game's format; play them, then choose **Save to Project**,
**Save and Place in Map** or **Open in Audio Editor**. See
[AI assistant](manual/ai.md#generate-levels-textures-and-sounds).

### Command-line equivalents

```sh
vibestudio --cli asset audio-wav ./pak0.pk3 sound/items/pickup.wav --output ./pickup.wav --dry-run
vibestudio --cli asset audio-edit ./sound.wav --operation normalize --db -1 --output ./normalized.wav --dry-run --json
vibestudio --cli asset audio-export ./sound.vsaudio --preset doom --output ./DSWIND.dmx --dry-run --json
vibestudio --cli asset audio-analyze ./sound.vsaudio --json
```

<details>
<summary>All audio commands</summary>

| Task | Command |
| --- | --- |
| Export a package sound as WAV | `asset audio-wav` |
| Inspect, import or convert to a `.vsaudio` project | `asset audio-project` |
| Create an empty or silent sound | `asset audio-new` |
| Edit samples | `asset audio-edit` |
| Deliver with a game preset | `asset audio-export` |
| Measure peaks and loudness | `asset audio-analyze` |
| Read or replace cues and loops | `asset audio-markers` |
| Work with multitrack sessions | `asset audio-session` |
| Review recordings | `asset audio-take`, `asset audio-recording` |
| Review or discard recovery copies | `asset audio-recoveries` |
| Generate a sound effect | `asset audio-generate` |
| Place a Quake II or III speaker | `map place-sound` |

</details>

These commands never open an audio device. See [Command line](manual/cli.md) for
options and exit codes.

### Learn more

- [Audio editor reference](AUDIO_EDITOR.md): formats, limits, sessions, effects and delivery.
- [Recording and review](AUDIO_DUPLEX.md): how multitrack recording works and its platform limits.
- [AI automation](AI_AUTOMATION.md#generating-sounds): how the Sound Generator works.

## Code, scripts and shaders

The **Code** page edits the text files in your project, such as QuakeC,
configs, shader scripts and entity definitions, with highlighting, search,
navigation and careful saves. The **Materials** page reads Quake III shader
scripts and checks the textures they use.

> [!NOTE]
> **Status: Partial.** The editor, project search and built-in navigation work
> and have automated tests, but have not been proven in real projects. Live
> diagnostics and semantic completion need a language server you install
> yourself, and C and C++ files have no built-in highlighting. The Shaders page
> reads and checks scripts but cannot edit them graphically yet.

### Open files

Choose **Code** on the rail, or press <kbd>Ctrl</kbd>+<kbd>7</kbd>
(<kbd>Cmd</kbd> on macOS). With a project open:

- The **Files** tab lists the project's source and text files. The list fills
  in the background; **Refresh Tree** scans again after you add files outside
  VibeStudio.
- The **Outline** tab lists the functions, shaders or entity classes in the
  open file; select one to jump to it.
- **Go to File** (<kbd>Ctrl</kbd>+<kbd>P</kbd>) opens any project file from
  anywhere in the studio.
- **New Text File** (<kbd>Ctrl</kbd>+<kbd>N</kbd> on this page) starts an
  untitled tab.

Each file opens in its own tab, and a dot marks unsaved changes.
<kbd>Ctrl</kbd>+<kbd>Page Down</kbd> and <kbd>Ctrl</kbd>+<kbd>Page Up</kbd>
move between tabs, and <kbd>Ctrl</kbd>+<kbd>F4</kbd> closes one. The bar above
the editor shows the file's folders; click a folder to open another file from
it. Scripts opened from inside a package are read-only copies: extract them, or
use **Save File As…**, to edit them.

### Supported files

| Language | Files | Highlighting |
| --- | --- | --- |
| QuakeC | `.qc`, `.qh`, `progs.src` | Yes |
| Console config | `.cfg`, `.rc`, `.scr`, `.skin` | Yes |
| idTech3 shader | `.shader` | Yes |
| Entity definitions | `.def`, `.fgd`, `.ent` | Yes |
| Key/value scripts | `.ini`, `.arena`, `.bot`, `.menu`, `.conf` | Yes |
| JSON | `.json` | Yes |
| Map source | `.map` | Yes |
| C and C++, Python, Lua, ACS, ZScript, plain text and others | `.c`, `.cpp`, `.h`, `.py`, `.lua`, `.acs`, `.zsc`, `.txt`, … | No |

Files open in UTF-8 (with or without a byte-order mark) or UTF-16 with a
byte-order mark. Other encodings, binary files and files over 4 MiB open
read-only, with an explanation. Maps opened with **Go to File** or by dropping
them on the window go to **Levels**; the **Files** tab opens them here as text.

### Edit text

The editor indents new lines to match, and <kbd>Tab</kbd> and
<kbd>Shift</kbd>+<kbd>Tab</kbd> indent or outdent selected lines. Because Tab
indents, <kbd>Ctrl</kbd>+<kbd>Tab</kbd> and
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> move the keyboard focus out of
the editor.

| Action | Keys |
| --- | --- |
| Toggle line comment | <kbd>Ctrl</kbd>+<kbd>/</kbd> |
| Duplicate lines | <kbd>Ctrl</kbd>+<kbd>D</kbd> |
| Move lines up or down | <kbd>Alt</kbd>+<kbd>Up</kbd>, <kbd>Alt</kbd>+<kbd>Down</kbd> |
| Fold or unfold a block | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>[</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>]</kbd> |
| Go to line | <kbd>Ctrl</kbd>+<kbd>L</kbd> |
| Go to symbol | <kbd>Ctrl</kbd>+<kbd>T</kbd> |
| Zoom the editor | <kbd>Ctrl</kbd>+<kbd>=</kbd>, <kbd>Ctrl</kbd>+<kbd>-</kbd>, or <kbd>Ctrl</kbd>+wheel |

You can also fold a block by clicking its marker in the line-number margin, or
use **View** > **Fold All** and **Unfold All**. **View** > **Sticky Headers**
keeps the first lines of the blocks you are scrolling through pinned above the
text.

### Find and replace

- <kbd>Ctrl</kbd>+<kbd>F</kbd> opens the find bar. <kbd>Enter</kbd> or
  <kbd>F3</kbd> finds the next match, <kbd>Shift</kbd>+<kbd>Enter</kbd> or
  <kbd>Shift</kbd>+<kbd>F3</kbd> the previous one, and <kbd>Esc</kbd> closes
  the bar. **Match case** narrows the search.
- <kbd>Ctrl</kbd>+<kbd>H</kbd> adds **Replace with**. **Replace**
  (<kbd>Enter</kbd>) replaces the current match and moves on; **Replace All**
  (<kbd>Ctrl</kbd>+<kbd>Enter</kbd>) replaces every match as one undo step.

To search the whole project:

1. Choose **Find in Project** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd>).
2. Type in **Find** and, if you like, set **Match case**, **Whole words**, and
   **Include** or **Exclude** file patterns such as `*.qc;*.qh`.
3. Choose **Search**. Results appear in **Search Results**; activate one to open
   its file at that line.
4. To replace, tick **Replace with**, type the replacement (leave it empty to
   delete the matches) and choose **Search** again to review every change
   before and after, then choose **Apply Preview…**.

Applying checks that no file has changed since the preview. Open files receive
ordinary unsaved, undoable edits; other files are saved directly in their
original encoding and line endings. Searches include unsaved text in open tabs.

### Navigate code

| Action | Keys |
| --- | --- |
| Go to Definition | <kbd>F12</kbd>, or <kbd>Ctrl</kbd>+click |
| Go back or forward | <kbd>Alt</kbd>+<kbd>Left</kbd>, <kbd>Alt</kbd>+<kbd>Right</kbd> |
| Find All References | <kbd>Shift</kbd>+<kbd>F12</kbd> |
| Complete Name | <kbd>Ctrl</kbd>+<kbd>Space</kbd> |
| Next or previous problem | <kbd>F8</kbd>, <kbd>Shift</kbd>+<kbd>F8</kbd> |

Without a language server, definitions come from a project index of names, so
they are textual; **Index Code** on the page header rebuilds it, and the
**Index** tab shows its results. Find All References then uses a whole-word
text search, and completion offers names from the file, the index and the
language's keywords. The **Problems** tab lists what VibeStudio's checks find
in the open file; problem lines are underlined, and the problem count above the
editor jumps to the next one.

### Save your work

**Save** (<kbd>Ctrl</kbd>+<kbd>S</kbd>) keeps the file's encoding, byte-order
mark, line endings and final newline, and **Save File As…**
(<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd>) writes a copy.

- If the file changed on disk since you opened it, VibeStudio asks before
  writing; **Cancel** is the default and keeps both versions.
- When another program changes an open file, a tab without unsaved edits
  reloads by itself, and a tab with unsaved edits asks first.
- A deleted file can be written again from its tab with **Recreate**.
- If the file is a map with unsaved edits in **Levels**, save or discard those
  edits first.
- Unsaved work is checkpointed every few seconds. **File** >
  **Recover Text Documents…** lists the copies and restores one as an unsaved
  draft.

Builds and packages use the files on disk, so save before you compile or stage
a script.

### Connect a language server (optional)

A language server, such as `clangd` for C and C++, adds live diagnostics,
symbol information, parameter hints, smarter completion, formatting and
renaming. VibeStudio does not include or install one.

1. Open the **Language Server** tab below the editor.
2. Choose the server program by its full path, then enter the language
   identifier and the file extensions it handles.
3. Add any arguments, one per line, under **Arguments and log**.
4. Choose **Connect**. **Disconnect** stops the server.

The server runs with your account's permissions and sees the matching files in
your project, including unsaved text, so choose one you trust. It never starts
by itself. When connected, these commands use it:

| Command | Keys |
| --- | --- |
| Quick Info | <kbd>Ctrl</kbd>+<kbd>I</kbd> |
| Parameter Hints | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Space</kbd> |
| Format Document, Format Selection | <kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd>, <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F</kbd> |
| Rename Symbol | <kbd>F2</kbd> |
| Code Actions | <kbd>Ctrl</kbd>+<kbd>.</kbd> |

Rename and code actions show every proposed change in **Search Results** before
you apply them. For the AI-based alternative, right-click in the editor and
choose **Ask Assistant About Selection** (see [AI assistant](manual/ai.md)).

### Inspect shader scripts

Shader scripts and Doom 3 materials have their own page: **Materials** on the
rail (<kbd>Ctrl</kbd>+<kbd>8</kbd>) previews them live and edits them as text
or as nodes; see [Materials and shaders](manual/materials.md). Its **Script Outline**
tab keeps the older tree view of one shader script: choose **Open Script**, or
type a path and choose **Inspect**.

The outline lists every shader with its stages, blend modes and the textures
it references. Textures missing from the open package are marked, so open the
game's or project's package first. The filter searches shader names, stages
and textures.

- Activate a texture row to show the texture, or a shader or stage row to open
  the script in **Code** at that line.
- Right-click a row for **Show Texture**, **Open in Code Editor**, and
  **Copy Shader Name** or **Copy Texture Path**.

The outline is read-only; edit scripts on the **Materials** tab or as plain
text in **Code**. `shader set-stage` on the command line changes one stage
setting and writes the result to a new file; the `material` commands cover
much more (see [Materials and shaders](manual/materials.md)).

### Command-line equivalents

```sh
vibestudio --cli code index ./mymod --find monster --json
vibestudio --cli asset find ./mymod --find player --whole-word --include "*.qc;*.qh" --json
vibestudio --cli shader inspect ./scripts/common.shader --package ./baseq3 --json
```

<details>
<summary>All code and shader commands</summary>

| Task | Command |
| --- | --- |
| Index symbols and diagnostics | `code index` |
| List project source files | `code files` |
| Check a file's encoding and hash | `code text-info` |
| Create, save or copy a file safely | `code text-create`, `code text-save`, `code text-save-as` |
| List or restore recovery copies | `code recoveries`, `code text-recover` |
| Use a language server without the GUI | `code language-server` |
| Search or replace across the project | `asset find`, `asset replace` |
| Read a shader script and check its textures | `shader inspect` |
| Change one shader stage setting | `shader set-stage` |
| List entity classes, check a map's entities | `entity definitions`, `entity validate` |

</details>

The `code text-…` commands and `asset replace` only preview until you add
`--write`. See [Command line](manual/cli.md) for details.

### Learn more

- [Code editor reference](CODE_EDITOR.md): encodings, saving, recovery and limits.
- [Local language services](LANGUAGE_SERVICES.md): setting up servers and what each feature does.
- [Project search](PROJECT_SEARCH.md): scope, limits and partial writes.

## Build and launch

The Build page compiles your map with VibeStudio's own VibeMap2 and VibeMap3 compilers or the Doom
node builders, shows each problem where it happens, and starts the game with the result. This page
covers setting up the compilers, running a build, fixing problems and leaks, and launching a test.

> [!NOTE]
> **Status: Partial.** Build pipelines for Quake, Quake III and Doom maps run VibeMap2, VibeMap3,
> ZDBSP and ZokumBSP and report problems, leaks and outputs. VibeStudio does not include the
> compiler programs yet, and only a few compiler and game combinations have been tested end to end.
> On 2026-10-08 VibeMap2 and VibeMap3 passed VibeStudio's Quake, Quake II and Quake III prepared-build
> tests on Windows; other platforms, games and the older stock-compiler tests are still to be repeated.

### Install the compilers

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

### Point VibeStudio at your compilers

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
[Projects and game installations](manual/projects.md#describe-the-project-with-a-manifest).

**Compiler profiles**, below the tools, lists every single compiler step, such as
**VibeMap2 bsp** or **VibeMap3 light**, and whether its tool is ready. **Run Profile**
(<kbd>Ctrl</kbd>+<kbd>R</kbd>) runs the selected step, **Copy CLI**
(<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd>) copies a matching `vibestudio --cli compiler run`
command, and **Copy Manifest** copies the step's command manifest.

### Build a map

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

### Fix build problems

After a run, the **Problems** tab lists every warning and error the compilers reported, with its stage,
file and line. Press <kbd>Enter</kbd> on a problem, or double-click it, to go to it:

- A line inside a brush, patch or entity of the open map selects that object on the Levels page.
- A line in another text file, such as a shader script, opens the file on the Code page at that line.
- Anything else shows the output of the stage that reported it.

Right-click a problem for **Copy Message**, which copies the line exactly as the compiler printed it,
and **Copy All Problems**. Press <kbd>F4</kbd> and <kbd>Shift</kbd>+<kbd>F4</kbd> on any page to step
through the last build's problems. **Explain** on the toolbar opens the Assistant with the problems
ready to ask about; AI stays off unless you turn it on, as described in [AI assistant](manual/ai.md).

Known-issue explanations use the VibeMap2 3.1.0 audit and keep ericw-tools issue
numbers for reference. Reports fixed in that fork no longer add plan or map
preflight warnings. Platform requirements are guidance; retained historical
reports still need case-specific verification. Actual compiler warnings and
errors remain visible. See the [issue reconciliation](plans/ericw-tools-remaining-bugs-resolution.md)
for the evidence and remaining limits, including older/custom compiler builds.

VibeMap2's `-embedsettings` option can store effective lighting settings in the
BSP; it is opt-in. Keep VibeStudio's command manifests for the complete build.

#### Find a leak

A leaking map, one that is not sealed from the void, puts the leak at the top of **Problems**. With
the map open on the Levels page, activate the leak to draw the compiler's leak trail over the map and
frame it. If the compiler wrote no leak point file, the problem says so. A leak also stops the VIS
stage, because the compiler keeps no portal file to work from.

To draw a trail yourself, choose **Build** > **Load Leak Trail…** and pick a `.pts` or `.lin` file;
**Build** > **Clear Leak Trail** removes it. **Inspect Artifacts** on the Build header reads the
compiled BSP and any leak or portal file beside it.

### Launch the game

Launching needs a game installation profile; add one on the Workspace page first, as described in
[Projects and game installations](manual/projects.md#add-your-game-installations). VibeStudio launches the
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

#### Allow test maps

Quake-family engines only load maps from a game folder's `maps` folder. With **Copy build to game**
ticked, VibeStudio copies the built BSP, and any `.lit` or `.lux` lighting file beside it, into
`<installation>/<game folder>/maps` before launching, using the base game's folder when
**Game folder** is empty. Doom source ports read the built WAD where it is, so nothing is copied for
Doom.

Installation profiles are read-only to start with. The first time a launch needs to copy a map,
VibeStudio asks whether to **Allow Test Maps** for that installation. If you allow it, the profile
keeps the permission and VibeStudio does not ask again. Nothing else in the installation is written.
To take the permission back, clear **Allow test maps** for the installation on the Workspace page.

### Build and launch in one step

**Build and Launch** (<kbd>F5</kbd>) on the Build header runs the pipeline and, when it succeeds,
launches the game with the built map, with the same confirmation as **Launch Game**. This is the
quickest edit, build and test loop. The keys on this page are the defaults: an editor profile or your
own key choices can change them, and **Help** > **Keyboard Shortcuts** lists the keys in effect.

### Share the result

- **Package Map** on the Build header releases the pipeline's map for players: its build, the files
  the engine loads beside it and every custom asset it uses, without the game's own files, with a
  readme and release notes. See [Package and release](manual/releases.md).
- **Add to Package** stages the built map into the open package under `maps/`, ready for Save As; see
  [Packages](manual/packages.md#save-a-new-package).
- **Copy Commands** copies every stage's command line, so the same build can run from a shell or CI.

For Quake-family maps, **Build** > **Prepare Build Workspace…** captures the current map, including
unsaved edits, together with the open package or draft, as verified copies in a new folder. Choose
**Use in Build** to build from that snapshot, so the build uses exactly the assets you staged.
**Build** > **Publish Prepared Build…** then writes a PAK or PK3, and **Build** >
**Deploy Prepared Build…** reviews and installs that package into a game folder, optionally
launching it. See [prepared builds](LEVEL_EDITOR.md#prepared-builds-with-current-assets) for the
details.

### Command-line equivalents

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

### Learn more

- [Compiler integration](COMPILER_INTEGRATION.md): profiles, diagnostics, leak detection and the
  licence boundary.
- [Game installations](GAME_INSTALLATIONS.md): profiles and the read-only rule.
- [CLI strategy](CLI_STRATEGY.md): every `build`, `compiler` and `launch` option.

## AI assistant (optional)

VibeStudio can ask a text, image or sound model for help: questions about your
map, code or build, generated levels, textures and sounds, and proposed map
edits. All of it is optional, and nothing is sent anywhere until you turn it
on.

> [!NOTE]
> **Status: Partial.** The Assistant, the generators and the OpenAI, Claude,
> Gemini, ElevenLabs, local and custom connectors are implemented and tested
> offline against each provider's request and answer formats, but they have
> not been proven by users in real projects. The Claude Code, Codex and Custom
> Command connectors are tested against a stand-in program, not the real
> tools. The Local GGUF connector has a test that runs a small model, but only
> in builds with ggml when you name a model, so CI does not run it. Meshy,
> voice features and agentic workflows are Planned.

### AI-free mode

AI-free mode is on when you install VibeStudio. While it is on, no request goes
to any model, local or cloud, and the status bar shows **AI-free**. Editing,
building, packaging, validation, launching and the command line never need
AI, and every generator has a local path that works without it.

A project can also switch AI off for itself, whatever the studio setting:

```sh
vibestudio --cli project init ./mymod --project-ai-free on
```

A project can only make AI stricter; it never turns AI on. Run the same command
with `off` to remove the restriction.

### Choose connectors and models

Everything below is set in **Settings** > **AI and Automation**. Its first
group holds three switches:

- **AI-free mode** turns every AI feature off.
- **Allow cloud AI connectors** lets AI features use connectors that send
  requests off your computer: providers with your API key, Claude Code and
  Codex with your own login, and Custom Command. A project's first request to
  each of them waits for your approval (see
  [Preview and approve requests](manual/ai.md#preview-and-approve-requests)).
- **Allow agentic workflows** is Planned and does nothing yet.

**Text Model** sets up the connector that the Assistant, **Generate Level**,
**Edit Map with AI** and `ai ask` use, one connector at a time:

| Field | What it holds |
| --- | --- |
| **Connector** | The connector whose settings are shown below. |
| **Model** | The model's name as the provider or runtime lists it. For **Local GGUF Model (ggml)**, the `.gguf` file, which **Browse** picks. |
| **Endpoint** | The provider's or runtime's address; empty uses the provider's own. For Claude Code, Codex and Custom Command it reads **Program**: the tool's program, which **Browse** picks for Claude Code and Codex, or Custom Command's command line. The GGUF connector has none. |
| **Key** | Where the API key comes from. For Claude Code, Codex and Custom Command it reads **Sign-in**: the tool keeps its own login. |
| **Status** | What text features use now, and what stops them. |

- **Use This Connector** makes the connector shown the one text features ask.
  It becomes **Reasoning**, or **Local** while cloud connectors are off, so a
  cloud **Reasoning** connector stays chosen for when you allow them again.
  The button is hidden while text features already use that connector, and
  greyed, with the reason in its tooltip, when the settings rule it out.
- **Test Connection** asks the connector shown to reply OK and sends nothing
  from your project.
- **Ask Again Before Sending** forgets every approval.

**Preferred Connectors** chooses a connector for each kind of work:

| Row | What uses it |
| --- | --- |
| **Reasoning** | Every text feature. |
| **Image** | **Generate Texture** and `ai image`, with its model under **Image Model**. Only connectors that can draw pictures are listed. |
| **Audio** | **Generate Sound**, with its model under **Sound Model**. Only connectors that make sounds are listed. |
| **Local** | Text features while **Allow cloud AI connectors** is off. Lists **Local/Offline Runtime** and **Local GGUF Model (ggml)**. |
| **Coding (planned)**, **Vision (planned)**, **Voice (planned)**, **3D (planned)**, **Embeddings (planned)** | Nothing yet. |

An entry the settings rule out stays listed, greyed, with the reason after its
name and in its tooltip: **(needs cloud connectors)** for a connector that
leaves your computer while cloud connectors are off, and
**(not in this build)** for the GGUF connector in a build without ggml.

### Use a local model

A local runtime keeps every request on your computer and needs no key.

1. Start your runtime, such as Ollama, LM Studio or llama.cpp's server, and
   make sure it has a model.
2. Open **Settings** > **AI and Automation** and untick **AI-free mode**.
3. Under **Text Model**, pick **Local/Offline Runtime** in **Connector**, and
   type the **Model** name exactly as your runtime lists it (for Ollama, as
   `ollama list` shows it).
4. Leave **Endpoint** empty for Ollama's `http://localhost:11434/v1`, or enter
   your runtime's own address.
5. Choose **Use This Connector**.
6. Choose **Test Connection**. It asks the model to reply OK and sends nothing
   from your project.

For pictures, set **Preferred Connectors** > **Image** to
**Local/Offline Runtime**. Under **Image Model**, its **Endpoint** defaults to a
Stable Diffusion web UI, such as AUTOMATIC1111 or Forge, at
`http://127.0.0.1:7860`.

### Use your Claude or ChatGPT login

If you have a Claude or ChatGPT plan, VibeStudio can ask its models through the
tools you already sign in to, with no API key: **Claude Code** for Claude (for
example Opus 5.5), and **Codex** for OpenAI's models. Every text feature works
this way: the Assistant, both level generators, **Edit Map with AI**, and
**Test Connection**.

1. Install the tool and sign in once in a terminal: `claude auth login` for
   Claude Code, or `codex login` for Codex.
2. Open **Settings** > **AI and Automation**, untick **AI-free mode** and tick
   **Allow cloud AI connectors**. Your login still sends requests to Anthropic
   or OpenAI, so they count as cloud requests.
3. Under **Text Model**, pick **Claude Code** or **Codex** in **Connector**.
   **Model** is `claude-opus-5-5` for Claude Code unless you name another (or
   an alias such as `sonnet`); Codex uses its own default unless you name one.
   **Program** shows where the tool was found; choose **Browse** to use
   another copy.
4. Choose **Use This Connector**.
5. Choose **Test Connection**. It first checks that the tool is signed in and
   says how, then asks it to reply OK. If the tool is signed in with an API
   key, it warns you that requests are billed to that key, not to your plan.

VibeStudio runs the tool in a new, empty folder, so it can only answer:

- **Claude Code** runs in print mode with `--safe-mode`: no built-in tools,
  MCP servers, hooks, plugins, `CLAUDE.md` files, memory or saved session.
- **Codex** runs `codex exec` with `--ignore-user-config`, so your
  `config.toml`, with its MCP servers, hooks and plugins, is not loaded; your
  login still is. Its shell and other tools are switched off, it works in a
  read-only sandbox, and it runs with `--ephemeral` and `--ignore-rules`.
- Environment variables that would send requests elsewhere, or bill them to
  an API key, are not passed on. Claude Code gets no `ANTHROPIC_API_KEY`,
  `ANTHROPIC_AUTH_TOKEN`, `ANTHROPIC_BASE_URL`, `CLAUDE_CODE_USE_BEDROCK`,
  `CLAUDE_CODE_USE_VERTEX` or `CLAUDE_CODE_USE_FOUNDRY`; Codex gets no
  `CODEX_API_KEY`, `OPENAI_API_KEY` or `OPENAI_BASE_URL`. To use an API key,
  use the **Claude** or **OpenAI** connector; a **Custom Command** can run a
  tool any other way.
- Cancelling a request, or its timing out, ends the tool and everything it
  started, then removes the run folder.

VibeStudio never reads the tools' credentials. The first request from a
project shows exactly what will run, command and all, in the usual
**Send to …?** dialog. If a tool is too old for an option VibeStudio passes,
the request fails with "… is older than VibeStudio needs" and the command
that updates it.

**Custom Command** runs any other program that answers a prompt, such as
another vendor's command-line tool. VibeStudio cannot tell where the program
sends your prompt, so it counts as a cloud connector even when it runs on your
computer: it needs **Allow cloud AI connectors**, and the first request from a
project asks you first. Enter its command line as the **Program**, with
`{model}`, `{prompt_file}`, `{system_file}`, `{schema_file}` and
`{answer_file}` where the program expects them. **Model** fills `{model}`;
with no model set, `{model}` and the option before it, such as `-m {model}`,
are left out, so the program uses its own default. Without `{prompt_file}`,
the prompt goes to the program's standard input; without `{answer_file}`, its
standard output is the answer. For example, `gemini -m {model}` runs Gemini
CLI with the prompt on standard input. On Windows, name an `.exe`: tools that
npm installed as `.cmd` files work, other batch files do not.

These connectors answer text only. For pictures, set
**Preferred Connectors** > **Image** and its model under **Image Model**: OpenAI,
Gemini, a local runtime or a custom endpoint. For sounds, set **Audio** and its
model under **Sound Model**: ElevenLabs or a custom endpoint. Each generator
also has its own path with no AI.

### Run a GGUF model inside VibeStudio

> [!NOTE]
> **Status: Partial.** Only builds you make yourself with `-Dggml=enabled` can
> run GGUF models; the released packages are built without it.
> **Local GGUF Model (ggml)** still appears in every build's connector lists.
> In a build without ggml, **Preferred Connectors** greys it with
> **(not in this build)**, and its **Key** line and **Test Connection** say how
> to get the runtime.

A build made with `-Dggml=enabled` (see
[Building from source](manual/building-from-source.md#build-with-ggml)) runs a GGUF
language model on the CPU, with no server and nothing leaving your computer.
In other builds, run llama.cpp's own server and use **Local/Offline Runtime**
(see [Use a local model](manual/ai.md#use-a-local-model)).

1. Download a GGUF chat model. Small models are quick on a CPU but plan less
   well.
2. Untick **AI-free mode**. Under **Text Model**, pick
   **Local GGUF Model (ggml)** in **Connector**, then choose **Browse** next to
   **Model** and pick the `.gguf` file. There is no **Endpoint**.
3. Choose **Use This Connector**. With cloud connectors off it becomes
   **Local**, and a cloud **Reasoning** connector stays chosen for when you
   allow them again.
4. Choose **Test Connection**.

The model stays loaded between requests, and its file's SHA-256 is worked out
once. Answers are deterministic: the same question gives the same answer. The
context window is the model's own, up to 32,768 tokens, and the same for every
request. An answer stops at the output limit the feature asks for, at most
32,768 tokens, and says so only when it was actually cut. A request waiting
for another can be cancelled at once, and so can loading a model. When a
feature asks for a structured answer, such as a level plan, the model can only
write text that fits it. Text in your messages that spells out the model's own
control tokens is removed before the model reads it. Quake-MapGen plans with
its own grammar and records the model file's SHA-256 in its report.

### Use a cloud provider

1. Put your provider's API key in an environment variable for your user
   account (see the table below), then restart VibeStudio so it can read it.
2. Open **Settings** > **AI and Automation**, untick **AI-free mode** and tick
   **Allow cloud AI connectors**.
3. Under **Text Model**, pick the provider in **Connector** and type the
   **Model** name your provider uses. Leave **Endpoint** empty to use the
   provider's own address. The **Key** line says whether the variable was found.
4. Choose **Use This Connector**. For generated pictures or sounds, set
   **Preferred Connectors** > **Image** or **Audio** too.
5. Choose **Test Connection**.

The ID is what the command line takes, as in `--provider claude-code` or
`--set-ai-reasoning local-gguf`.

| Connector | ID | What it does | Key variable | Status |
| --- | --- | --- | --- | --- |
| **OpenAI** | `openai` | Text questions and pictures | `OPENAI_API_KEY` | Available |
| **Claude** | `claude` | Text questions | `ANTHROPIC_API_KEY` | Available |
| **Gemini** | `gemini` | Text questions and pictures | `GEMINI_API_KEY` | Available |
| **ElevenLabs** | `elevenlabs` | Sound effects | `ELEVENLABS_API_KEY` | Available |
| **Custom HTTP Connector** | `custom-http` | Any OpenAI-compatible text or picture endpoint, such as a team proxy, and sound endpoints that take ElevenLabs' request | None | Available |
| **Local/Offline Runtime** | `local-offline` | Text and pictures from programs on your computer | None | Available |
| **Claude Code** | `claude-code` | Text questions, through your Claude plan | None: the tool's own login | Partial |
| **Codex** | `codex` | Text questions, through your ChatGPT plan | None: the tool's own login | Partial |
| **Custom Command** | `custom-command` | Text questions from any program you name; needs cloud connectors allowed | None | Partial |
| **Local GGUF Model (ggml)** | `local-gguf` | Text questions from a `.gguf` file, only in builds made with `-Dggml=enabled`, not in the released packages | None | Partial |
| **Meshy** | `meshy` | Listed for 3D and pictures; sends nothing yet | `MESHY_API_KEY` | Planned |

VibeStudio reads keys only from these environment variables. It never asks you
to type a key into the studio and never stores one in its settings, projects or
logs. Do not paste keys into a question, a map, a script or a context file.

### Ask the Assistant

Open the **Assistant** panel from **View** > **Assistant Panel** or the
command palette (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd>; press
<kbd>Cmd</kbd> instead of <kbd>Ctrl</kbd> on macOS). These open it with a
question already proposed:

- **Explain** on the **Build** page, or **Explain with Assistant** on a build
  problem, asks about the last build's problems.
- **Ask Assistant About Selection**, or **Ask Assistant About This File** when
  nothing is selected, asks about the code you right-click in the **Code**
  editor.

Then:

1. Under **Send with the question**, tick what the model should see.
2. Type your question and choose **Send** (<kbd>Ctrl</kbd>+<kbd>Enter</kbd>).
3. Read the answer, with its token counts and the time it took. **Copy** copies
   it, and **New** starts a fresh conversation.

While the model works, the panel counts the seconds and the **Activity Center**
shows the request; **Cancel** or <kbd>Esc</kbd> stops it. Links in answers are
shown and can be copied, but never open by themselves. In AI-free mode, the
panel opens and explains which setting to change.

### Choose what is sent

Only your question and the items you tick are sent, along with VibeStudio's
own instructions to the model. The page you are on decides what starts ticked:

| Item | What it contains | Ticked at first on |
| --- | --- | --- |
| Selected code, or the open file | The selection, or the file's text | **Code** |
| The map | The open map's summary and health report | **Levels** |
| Selected map objects | The properties of the selection | **Levels** |
| Build log | The last build's problems and the last lines of each tool's output | **Build** |
| The project | The project manifest | Never |

Point at an item to preview its first lines. Before anything leaves, paths
inside your project become `<project>`, paths in your home folder become `~`,
and text shaped like a common provider key becomes `***`. Long items are cut to
their first lines, and the request says so. The key check looks for known
patterns only, so review what you tick.

### Preview and approve requests

**Preview** shows the exact request, without your key, and sends nothing:
readable by default, or the raw request body. For Claude Code, Codex and
Custom Command it shows the command that would run, with the files and input
it would get; for the GGUF connector, what the model would be given.

The first time a project sends to an endpoint off your computer, VibeStudio
shows the request in a **Send to …?** dialog, where **Cancel** is the default
and **Send** sends it. The dialog says where the request goes: to the
provider, with your API key used only to sign in; to Anthropic or OpenAI
through Claude Code or Codex, which sign in with your own login in an empty
folder with their tools turned off; or to your custom command, which sends it
wherever it is set up to. The box below the request,
**Don't ask again for this project and** the host's name, is ticked; untick it
to be asked every time. **Ask Again Before Sending** in **Settings** >
**AI and Automation** forgets every approval. Endpoints on your own computer
(`localhost`, loopback addresses and `.localhost` names) never ask, because
nothing leaves it.

### Generate levels, textures and sounds

Each generator has a local way to work with no AI, and an AI way that uses the
connectors above:

| Command | Opens from | Without AI | With AI |
| --- | --- | --- | --- |
| **Generate Level…** | **Generate** on **Levels** | Rules on your computer plan the rooms, with either generator | The text model plans the rooms, and the plan is checked and repaired |
| **Edit Map with AI…** | **Edit with AI** on **Levels** | **Load Proposal…** replays a saved proposal | The text model proposes edits to the open Quake-family map |
| **Generate Texture…** | **Generate** on **Textures** | Starts from a picture of your own | The image model draws variants |
| **Generate Sound…** | **Generate** on **Audio** | The built-in synthesizer makes the sound | The sound model makes it |

Nothing is written or applied until you choose it:

- **Generate Level** shows a layout preview, the plan and its notes.
  **Open in Levels** opens the result as a new, unsaved map; **Save As…** and
  **Save Plan…** write files. With **Generator: Quake-MapGen**, **Details**
  adds **Analysis** (navigation, visibility, lighting and travel findings) and
  **Report**, and **Save Report…** replaces **Save Plan…**.
- **Edit with AI** asks you **What should change?**, then **Ask for Edits**
  lists each proposed edit as **Ready** or **Blocked**, with the reason.
  Untick any you do not want, then choose **Apply Checked**: each edit is an
  ordinary step you can undo. **Save Proposal…** keeps the list. Doom maps are
  not supported.
- **Generate Texture** shows its variants tiled 2 × 2 so seams show.
  **Save to Project** writes the chosen one where the game reads it,
  **Save and Apply to Map Selection** also puts it on the selected faces, and
  **Open in Texture Editor** continues by hand.
- **Generate Sound** lets you play each variant. **Save to Project**,
  **Save and Place in Map** (a `target_speaker` in a Quake II or Quake III map)
  and **Open in Audio Editor** take it further.

When you choose a generator's AI path (always, in **Edit Map with AI**), two
buttons appear:

- **Preview Request…** shows what would go to the model and sends nothing:
  for a provider, the request without your API key; for Claude Code, Codex
  and Custom Command, the command that would run, with the files and input it
  would get; for the GGUF connector, what the model on your computer would be
  given. If the connection is blocked, it says why.
- **AI Settings…** opens **Settings** > **AI and Automation**. Open generator
  windows follow any change to AI settings at once. When the text model is
  ready, **Generate Level** names it in its choice:
  **The text model:** followed by the connector and model.

Like the Assistant, each generator asks before its first request to an
endpoint off your computer. Saved textures and sounds record what made them in
the project's `.vibestudio/generated` folder.

### When a request cannot be sent

The Assistant, the generators, **Status** and **Test Connection** say what is
missing:

| Message | What to do |
| --- | --- |
| AI-free mode is on. | Untick **AI-free mode** in **Settings** > **AI and Automation**. |
| No connector is chosen. | Pick a **Reasoning** or **Local** connector, or choose **Use This Connector**. |
| … has no endpoint. | Enter the **Endpoint**; a custom connector has no default. |
| … sends to …, off this machine, and cloud connectors are not allowed. | Tick **Allow cloud AI connectors**, or use a local runtime. Claude Code, Codex and Custom Command always need it. |
| No model is set for … | Type the **Model** name. |
| … needs an API key in the … environment variable, which is not set. | Set the variable, then restart VibeStudio. |
| … was not found (looked in: …). | Install Claude Code or Codex, or choose **Browse** next to **Program** and pick the program. |
| … is not signed in. | Run `claude auth login` or `codex login` in a terminal. |
| … is older than VibeStudio needs … | Update the tool: `claude update` for Claude Code, `npm install -g @openai/codex@latest` for Codex. |
| No command is set for Custom Command. | Enter the command line as the **Program**. |
| … is a batch file VibeStudio cannot run safely. | Point **Program** at the `.exe`, or at `node` and the tool's script. |
| This build of VibeStudio has no ggml runtime. | Use a build made with `-Dggml=enabled`, or run llama.cpp's server and use **Local/Offline Runtime**. |
| No GGUF model file is set. | Choose **Browse** next to **Model** and pick a `.gguf` file. The same goes for "… does not exist" and "… is not a .gguf model file". |
| This project turns AI off in its manifest. | Run `project init` with `--project-ai-free off`, as shown under [AI-free mode](manual/ai.md#ai-free-mode). |

### Privacy at a glance

- AI is off until you untick **AI-free mode**, and a project can keep it off.
- Cloud providers, Claude Code, Codex and Custom Command also need
  **Allow cloud AI connectors**.
- Only your question and the items you tick are sent, with project and home
  folder paths shortened and key-shaped text removed.
- **Preview** and **Preview Request…** show the exact request without sending
  it.
- The first request from each project to each outside host needs your
  approval, and **Ask Again Before Sending** resets every approval.
- Keys come only from environment variables and are never stored or shown.
- Generated work is reviewed before anything is written or applied.

### Command-line equivalents

```sh
vibestudio --cli ai status --json
vibestudio --cli ai test-connection --json
vibestudio --cli ai ask --prompt "why does qbsp report a leak?" --context-file build/qbsp.log --dry-run
```

`--dry-run` prints the request without your key and sends nothing. A command
that would send to an endpoint off your computer stops until you add `--yes`.
`ai status` prints `Text:`, `Images:` and `Sounds:` lines that say what each
feature would use now, and a `Connectors` list that gives each one's
`Status`: Available or Planned. `ai test-connection` checks that Claude Code
or Codex is signed in before it asks, as **Test Connection** does.

`--provider` takes a connector ID from the table under
[Use a cloud provider](manual/ai.md#use-a-cloud-provider). Text commands also take
`claude-code`, `codex`, `custom-command` and `local-gguf`; picture and sound
commands do not. For `claude-code`, `codex` and `custom-command`,
`--endpoint` is the program or command line; for `local-gguf`, `--model` is
the `.gguf` file:

```sh
vibestudio --cli ai test-connection --provider claude-code --json
vibestudio --cli ai ask --provider claude-code --model sonnet --prompt "why does qbsp report a leak?" --context-file build/qbsp.log --yes
vibestudio --cli map generate --generator quake-mapgen --prompt "Quake 2 deathmatch, 8 rooms" --planner ai --output maps/qmg.map --report maps/qmg.json --yes
```

<details>
<summary>All AI and generator commands</summary>

| Task | Command |
| --- | --- |
| Show AI settings, keys found and connectors | `ai status`, `ai connectors` |
| Check the text model answers | `ai test-connection` |
| Ask a question, with files as context | `ai ask` |
| Draw or edit pictures | `ai image` |
| Generate or plan a level | `map generate`, `map plan`; `--generator quake-mapgen` for Quake-MapGen, `--ranker ggml` in ggml builds |
| Propose and apply map edits | `map ai-edit` |
| Generate a texture, or its companion maps | `texture generate`, `texture derive` |
| Generate a sound effect | `asset audio-generate` |

</details>

Settings have matching options, such as `--set-ai-free off`,
`--set-ai-cloud on`, `--set-ai-reasoning claude-code`,
`--set-ai-model claude-code=sonnet`, `--set-ai-model local-gguf=<path to .gguf>`
and `--set-ai-endpoint codex=<path to codex>`. `--set-ai-local` takes only the
connectors that run on your computer, `local-offline` and `local-gguf`. After
saving, the command prints the same `Text:`, `Images:` and `Sounds:` lines as
`ai status`. The other `ai` commands, such as
`ai explain-log`, are early rule-based helpers that run on your computer and
send nothing. See [Command line](manual/cli.md).

### Learn more

- [AI automation](AI_AUTOMATION.md): connectors, generators and their design rules.
- [First-run setup](manual/first-run.md): the AI step of the setup checklist.

## Accessibility and languages

VibeStudio lets you change contrast, text size, colours, motion, focus
visibility and speech, and use the whole studio from the keyboard. This page
also covers interface languages, right-to-left layouts and region formats.

> [!NOTE]
> **Status: Partial.** Every option on this page is implemented and covered by
> automated tests, but testing with real screen readers, physical keyboards and
> each operating system is still to come. The interface is prepared for 47
> languages, but no translation is finished yet, so menus and messages appear
> in English.

### Open the settings

Choose **Settings** on the rail, or press <kbd>Ctrl</kbd>+<kbd>0</kbd>
(<kbd>Cmd</kbd> on macOS). Two categories hold these options:

- **Appearance and Language**: theme, text scale, density, typeface, spacing,
  language and region formats. <kbd>Ctrl</kbd>+<kbd>,</kbd> opens it directly.
- **Accessibility**: colour vision, focus and cursor, motion, timing, alerts,
  screen readers and text-to-speech. **Tools** > **Accessibility Settings**
  opens it directly.

Every option takes effect at once, except the language, and is saved with your
other preferences. The **Search settings** field on the Settings page
(<kbd>Ctrl</kbd>+<kbd>F</kbd>) finds an option by name, and <kbd>Enter</kbd>
moves to it. The first step of [first-run setup](manual/first-run.md) leads to the
same options.

### Theme and contrast

**Theme** offers **Dark** (the default), **System**, **Light**,
**High Contrast Dark** and **High Contrast Light**. **System** follows your
desktop's light or dark colours and, in builds made with Qt 6.10 or later, its
high-contrast mode. **View** > **Toggle High Contrast** switches between a
high-contrast theme and the standard theme of the same lightness.

Status never relies on colour alone: every state also has its own symbol and
words, such as **AI-free** in the status bar.

The saved theme and text scale apply before the studio finishes its initial
row layout. Startup prepares each page before styling its complete widget tree
to reduce repeated work; no extra preference is needed.

### Text size, density and typeface

- **Text scale** goes from 100% to 200% in 25% steps and scales text, icons and
  controls. **View** > **Larger Text**, **Smaller Text** and
  **Reset Text Size** change it in steps.
- **Density**: **Comfortable** gives larger click targets, **Standard** is the
  default and **Compact** fits more on screen.
- **Typeface**: **System typeface**, or any font installed on your computer,
  such as Atkinson Hyperlegible, Lexend or OpenDyslexic. Code keeps its
  fixed-width font; zoom the code editor with
  <kbd>Ctrl</kbd>+<kbd>=</kbd> and <kbd>Ctrl</kbd>+<kbd>-</kbd>.
- **Wider letter and word spacing** adds the extra space between letters and
  words that WCAG 2.2 asks layouts to cope with; many readers with dyslexia
  find it easier to follow.
- **Navigation** chooses how the rail on the left uses its width:
  **Collapse to icons automatically**, **Always show labels** or
  **Icons only**.

### Colour and focus

On **Settings** > **Accessibility**, under **Vision**:

- **Colour vision** recolours status colours so the states you can tell apart
  are the ones that differ: **Standard colours**,
  **Red-green safe (protanopia, deuteranopia)**,
  **Blue-yellow safe (tritanopia)** or **Monochrome (no colour)**. Text and
  contrast stay as they are.
- **Reduce colour saturation** softens accent, selection and status colours
  towards grey without lowering contrast.
- **Thick focus outline** draws the keyboard focus outline three pixels wide
  around every control.
- **Thick text cursor** draws a three-pixel text cursor, growing with the text
  scale.

### Motion and timing

Under **Motion and Timing**:

- **Reduce motion** replaces spinners and animated progress bars with plain
  status text, opens the rail without sliding, switches pages without fading,
  and stops model animation playback.
- **Stop the text cursor blinking** keeps the cursor steady everywhere.
- **Status messages stay** sets how long status bar messages remain:
  **Standard**, **Three times longer** or **Until the next message**.

### Alerts and screen readers

Under **Alerts and Screen Readers**:

- **Announce results to screen readers** (on by default) asks your screen
  reader, such as Narrator, NVDA, JAWS, VoiceOver or Orca, to announce when a
  long task finishes or fails, and what the status bar reports, without moving
  focus. The Mesh Editor and the modeller's dialogs also announce their
  results and refusals (see
  [Use a screen reader, high contrast or reduced motion](manual/models.md#use-a-screen-reader-high-contrast-or-reduced-motion)).
- **Flash the taskbar when background work finishes** (on by default) flashes
  VibeStudio's taskbar or dock button when a long task ends while another
  window is in front.
- **Play sound cues for task results** (off by default) plays a rising tone
  when a task finishes, a level tone for a warning or cancellation, and a
  falling tone for a failure. Set **Cue volume** and try them with
  **Play Cues**.

Standard controls carry accessible names, and the map and model viewports,
charts and image previews describe their contents in text so a screen reader
can read them. Some custom controls do not report everything yet.

### Read aloud with text-to-speech

VibeStudio speaks with your computer's own voices; nothing goes to a cloud
voice service.

1. Under **Text to Speech**, tick **Read status changes aloud**.
2. Under **Read aloud**, choose what is spoken: **Long tasks that finish**,
   **Failures, warnings, and cancellations** (both on by default) and
   **Status bar messages**.
3. Choose a **Voice**, **Rate**, **Pitch** and **Volume**, then
   **Say Test Phrase** to hear them.

**Tools** > **Read Aloud** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>U</kbd>)
reads the selected text, the current line of an editor, the focused list row or
control, or else the latest status message, even when
**Read status changes aloud** is off. Press it again, or choose
**Stop Reading Aloud**, to stop. Password and key fields are never read.

| System | Speech engine | Notes |
| --- | --- | --- |
| Windows | Windows Speech API | Rate, pitch and volume all apply. |
| macOS | The system's `say` command | Pitch and volume follow the system voice settings. |
| Linux | Speech Dispatcher (`spd-say`), or eSpeak NG or eSpeak | Install one of them to hear speech. |

If no engine can be found, the **Text to Speech** section says why, and every
spoken message still appears on screen. Only the Windows engine has been
checked with real voices so far. To choose an engine, or to silence speech for
one run, set the `VIBESTUDIO_SPEECH_ENGINE` environment variable to `sapi`,
`say`, `spd-say`, `espeak-ng`, `espeak` or `none`.

### Work from the keyboard

Every command in the menus can be run from the keyboard:

- The command palette (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd> or
  <kbd>Ctrl</kbd>+<kbd>K</kbd>) runs any command by name, such as
  **Toggle High Contrast** or **Larger Text**.
- <kbd>Ctrl</kbd>+<kbd>1</kbd> to <kbd>Ctrl</kbd>+<kbd>0</kbd> switch pages,
  <kbd>Ctrl</kbd>+<kbd>F</kbd> jumps to the page's search or filter, and
  <kbd>Alt</kbd>+<kbd>Left</kbd> and <kbd>Alt</kbd>+<kbd>Right</kbd> go back
  and forward.
- The rail shows its labels while the keyboard is in it, and access keys are
  always underlined in menus.
- In the code editor <kbd>Tab</kbd> indents, so <kbd>Ctrl</kbd>+<kbd>Tab</kbd>
  and <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> move focus out of it.

**Help** > **Keyboard Shortcuts**, or **Keyboard Shortcuts…** on the
Accessibility settings, lists every command with its keys and the page they
work on. Select a command and choose **Change Keys…** to give it keys of your
own, or **Reset** to restore the default. Your keys take priority over the
level editor's profile keys (see [Editor profiles](manual/editor-profiles.md)).

Some controls inside pages are not yet reachable as commands, tab order has not
been checked everywhere, and a guided keyboard-only setup is still planned.
[A tour of the studio](manual/tour.md) lists the main shortcuts.

### Change the interface language

1. Open **Settings** > **Appearance and Language** and find
   **Language and Region**.
2. Choose a **Language**. The default, **System language** followed by your
   system's language name, follows your operating system whenever VibeStudio
   has that language.
3. Choose **Restart Now** in the notice that appears. The new language shows
   after the restart.

> [!IMPORTANT]
> The translation catalogues list every interface string in all 47 languages,
> but none has been translated and reviewed yet, so menus and messages stay in
> English whichever language you choose. Layout direction and region formats
> already follow your choice.

<details>
<summary>The 47 interface languages</summary>

English, Chinese (Simplified), Chinese (Traditional), Hindi, Spanish (Spain),
Spanish (Latin America), Arabic, French, Bengali, Portuguese (Brazil),
Portuguese (Portugal), Russian, Indonesian, Urdu, German, Japanese, Nigerian
Pidgin, Marathi, Vietnamese, Telugu, Hausa, Turkish, Punjabi, Swahili,
Filipino, Tamil, Persian, Korean, Thai, Malay, Italian, Gujarati, Amharic,
Kannada, Polish, Ukrainian, Romanian, Dutch, Greek, Hungarian, Czech, Swedish,
Bulgarian, Hebrew, Danish, Finnish and Norwegian Bokmål.

</details>

A pseudo-localisation catalogue, which accents and lengthens every string, is
also maintained so testers can check that layouts survive longer text; it is
not offered as a language.

### Right-to-left languages

Arabic, Urdu, Persian and Hebrew are right-to-left. After you restart into one
of them, the whole window mirrors, panels included. **Run Build Pipeline** and
**Launch Game** keep the same spacing at their leading edge in either direction.
The mirrored layout has been checked by automated tests, not yet by native readers.

### Region formats

**Region formats**, also under **Language and Region**, sets how numbers,
dates, times and sizes are written, separately from the language:
**System regional settings** (the default), **Match the interface language**,
or a specific region. A sample shows the result as you choose.

### Command-line equivalents

```sh
vibestudio --cli accessibility report
vibestudio --cli accessibility speak --test
vibestudio --cli localization report --locale ar --json
```

| Command | What it does |
| --- | --- |
| `accessibility report` | Prints every accessibility and language preference, the speech engine and its voice count. |
| `accessibility voices` | Lists the speech engine's voices. |
| `accessibility speak` | Speaks text, or the test phrase with `--test`. |
| `localization targets` | Lists the 47 languages, marking right-to-left ones. |
| `localization report` | Shows right-to-left coverage, formatting samples and translation catalogue status. |

Preferences can also be set with options such as `--set-theme`,
`--set-text-scale`, `--set-color-vision`, `--set-reduced-motion`, `--set-tts`,
`--set-locale` and `--set-region`; `--preferences-report` prints them. See
[Command line](manual/cli.md) for the full list.

### Learn more

- [Accessibility and localisation design](ACCESSIBILITY_LOCALIZATION.md): every option, the language list and test coverage.
- [Initial setup](INITIAL_SETUP.md): the first-run steps for language and accessibility.

## Command line

Model authoring `--dry-run` performs the shared source-save checks and complete
serialisation without creating output files, directories or save locks. Model
sources, auxiliary files and package/draft inputs are protected through path
aliases, including hard links to files inside source folders. Ordinary in-place `.mesh.json` editing remains supported
by commands that allow it. See [save and review output](manual/models.md#save-and-review-output)
for overwrite rules and the limits of a dry run.

`model uv-density` inspects existing UV islands using `--texture-size N` or
`WIDTHxHEIGHT` and an optional `--reference-frame`. Add `--target`,
`--mode individual|group` and a separate `.mesh.json` `--output` to match
density. `--faces` expands to complete islands; dry-run and output protection
share the editor's document service. See [texture density](MODEL_UV_DENSITY.md).

`model curve-transfer` also requires an output different from its input,
including path aliases, even with `--overwrite` or `--dry-run`.

`model tool --tool knife` cuts paths in a chosen orthographic or perspective
projection. Supply `--path 'u,v;u,v'` or a JSON `--path-file`, optionally with
`--through` and `--faces`. The reference pose determines cuts across all poses;
dry-run, source protection and output validation match the editor. See
[knife options](MODEL_KNIFE.md#automate-a-knife-cut).

`model tool --tool bevel-edges` creates chamfers or rounded bands from explicit
edges. Use `--width`, `--segments 1..32`, `--roundness 0..1`, `--tile-size` and
`--reference-frame`; topology changes across every pose. See
[edge-bevel options](MODEL_EDGE_BEVEL.md#automate-an-edge-bevel).

`model tool --tool rip` opens selected vertices or connected edge chains using
`--side-face N` and `--offset x,y,z`. `--opposite` swaps sides and `--fill`
adds gap faces. `--offset 0,0,0` without `--fill` saves a persistent split
without movement. Filled gaps require movement. The authoring source retains
connectivity through subsequent mesh and UV edits. Topology covers every retained pose; see
[rip options and limits](MODEL_RIP.md#automate-a-rip).

`model tool --tool spin` makes curved face or open-border extrusions, or radial
face copies with `--duplicate`. Set `--centre`, `--axis` or `--axis-vector`,
`--angle`, `--steps`, `--tile-size` and optional `--no-close`. All poses share
the topology; see [spin options](MODEL_SPIN.md#automate-a-spin).

`model tool --tool edge-slide` moves selected edge chains by a signed rail
fraction (`--slide`) or reference-pose distance (`--distance`). Current/all-frame
scope, validation and guarded output match the Mesh Editor. See
[edge-slide options](MODEL_EDGE_SLIDE.md#automate-an-edge-slide).

`model tool --tool symmetrize` on a skinned surface needs a mirror partner for
each off-centre joint the mirrored half uses: `--joint-map detect` reads them
from left and right joint names, keeping only pairs on opposite sides of the
chosen axis, `--joint-map left=right,...` names them (each pair applies both
ways), and `--keep-joint-names` keeps unmapped joints. Joints on the mirror
plane keep their names. Without a partner, or with a pair on the same side of
the plane, the edit is refused and names the joints. On skeletal models,
`model tool`, `model surfaces` and `model edit` print how each skinned surface's
weights were kept, interpolated, mirrored, rebound, merged or bound, and how
many blended vertices now have more joints than the surface had; `--json` adds
a `skinProvenance` object, and `model surfaces` marks each surface skinned or
not skinned. See
[skin weights through mesh edits](MODEL_SKIN_PROVENANCE.md#what-the-editor-and-cli-report).

`model tool --tool add` on a skinned surface needs a binding for the new
vertices: `--bind-joint NAME` ties them rigidly to one joint and
`--bind nearest-bones` binds each to its closest bones. `--new-surface NAME`
adds the primitive as its own surface instead, unskinned unless a binding is
also given (the editor's **Add as a new surface**). Without a binding on a skinned surface the edit is refused with
exit code 4. See [binding new geometry](MODEL_SKIN_PROVENANCE.md#bind-new-geometry).

```sh
vibestudio --cli model tool ./models/player.mesh.json --tool add --primitive cube --at 0,0,64 --size 12,12,12 --bind-joint head --output ./models/helmet.mesh.json
```

On skeletal models with a skinned surface, `model edit --operation
duplicate-frame`, `delete-frame`, `insert-inbetweens` and `copy-frame-pose`
refuse the bind frame and frames baked from a skeletal clip (exit code 4, with
the same message as the editor, naming the `model pose` operation to use);
on independent frames they keep every other frame's clip mapping. After
`model skinning --operation unbind` of every surface they apply to any frame.
See
[frame edits on skeletal models](MODEL_SKIN_PROVENANCE.md#frame-edits-on-skeletal-models).

`model select --select loop` or `--select ring` with `--edges a:b,c:d` (no
`--edge`) grows every listed edge to its loop or ring and joins the results,
as **Select Edge Loops of Selection** and **Select Edge Rings of Selection**
do in the Mesh Editor; without `--edges`, the edges between two `--vertices`
grow, as in the editor's vertex mode. Add `--extend` to keep the given
selection, as the editor does. JSON adds `seeds` and `refused` (each edge
without a loop or ring, with its `reason`); the command exits 4 when no edge
grows or no seed is given. `--edge a:b` still seeds a single loop. See
[selection operators](MODEL_TOOLS.md#selection).

`model curve-transfer` copies or reverses a range between clips of the current
rig. Select joints/channels, destination start and a time scale or rate match;
review the JSON destination changes with `--dry-run` before writing. It shares
the GUI's curve, pose and output protections. See
[range-transfer options](MODEL_ANIMATION_CURVES.md#automate-range-transfers).

`model curve-keys` selects stored keys across joints/channels and shares batch
retiming, duplication, removal and continuous-error reduction with **Edit
Animation Keys**. Use range or exact-sample selectors, inspect the JSON channel
reports and validate outputs with `--dry-run`. See
[key-group automation](MODEL_ANIMATION_CURVES.md#automate-key-groups).

`model curves` inspects, captures, edits and bakes persistent skeletal animation
curves. Key actions share the GUI's channel, interpolation, tangent and sampled
pose validation, with guarded `.mesh.json` outputs and `--dry-run`. See
[animation curves](MODEL_ANIMATION_CURVES.md#automate-curve-edits) for options
and examples. Native game exports receive sampled motion.

`model ik` solves two-bone inverse kinematics over an inclusive clip sample
range with an explicit end joint, target and pole. `--nearest-reachable` clamps
unreachable targets without stretching; JSON reports reach and residual distance
per sample. It shares the GUI pose service, source writer and dry-run validation.
See [inverse kinematics](MODEL_IK.md) for required options and examples.

`model build` first checks the model against a delivery target, the game the
file is for: `--target ID` names one (such as `quakespasm` for MDL, `q2pro` for
MD2, `et` or `ioquake3` for MD3, and `gzdoom` for MD3 and IQM). Without it the
target saved with the project that holds the source (or `--project DIR`) is
used, else the one the game installation suggests (the installation the
project links, else one of its game, else the selected one, as releases choose
it), else the format's original engine; JSON says which in `targetOrigin`. A model over any limit
exits 4 without writing, and JSON lists every blocker in `targetIssues` with
its surface, the faces or vertices concerned, value, maximum and fix.
`model targets` lists the targets with their limits and sources, or, given a
source, checks it against them (`--format` and `--target` narrow the list; with
`--target`, a model that is not ready exits 4). An id that names targets of
several formats, such as `ioquake3` for MD3 and IQM, needs `--format` to check a
model, and exits 2 without it. `model targets --project DIR` shows the target
each format uses in that project; add `--save --format md3 --target et` (and
`--strict` or `--no-strict`) to save a choice, or `--clear` to remove them. A
manifest that exists but cannot be read is never replaced: `--save` and
`--clear` exit 1 and name it.
`model prepare SOURCE --format md3 --output NEW.mesh.json` applies the fixes the
check suggests (split, join, decimate, fit to MD3's range, reduce joint
influences; `--skip` leaves kinds out) and exits 4 when limits remain; use
`--dry-run` to review. See [delivery targets](MODEL_TARGETS.md).

`model build` reports what each format leaves out or approximates in
`exportNotes`; the data a format cannot hold is also listed in
`exportOmissions`, and `--strict` refuses such an export (exit 4) without
writing, and `--no-strict` overrides a project that saves strict exports. For `.md5anim`, choose the clip with `--clip N|NAME`; `--frame N`
selects the clip that frame belongs to, and a model with several clips and
neither option exits 2. `--all-clips --output-dir DIR` writes one
`<clip>.md5anim` per clip after checking every destination, and
`--md5-frame-rate N` writes a whole frame rate in place of the clip's own, which
a fractional rate needs. With `--strict`, `--all-clips` prepares every clip
before refusing, and JSON lists each clip's `exportOmissions`, every omission
once at the top level and the `sharedNotes` every clip has. MDL, MD2 and MD3
keep clips a game rebuilds from the frame names, so an imported model exports
strictly again; renamed clips, clip rates and a skeleton are omissions. See
[what each export keeps](MODEL_FORMATS.md#what-each-export-keeps-changes-or-leaves-out).
For an MD3, `model build` also names the detail files beside the output that
the target still loads with the new model (JSON `leftInPlace`, whose
`consistent` is false when the game would read one wrongly), and notes when the
source stores detail levels it does not write.

`model lod` writes Quake III detail levels, `name_1.md3` and `name_2.md3`, beside
the base MD3. `--target` sets the MD3 delivery target and so how many levels the
game loads (the project's saved target when omitted); more levels than it loads
exit 2. `--levels` and `--ratio`, `--target-triangles 900,300`, or
`--level-source 2=PATH` for a hand-made level describe the levels, and a source
that stores its levels builds them when none of these is given;
`--save-recipe` stores the described levels in the source. Every file is
prepared and checked before any is written, including that each level has the
base model's frames and surface names; existing files need `--overwrite`, and
JSON gives each file's `committed` state (`written`, `failed` or `pending`), its
`exportNotes` and `exportOmissions`, the `existing` files, any `findings`, and
in `leftInPlace` the detail files past the set that stay and still load.
`--strict` refuses a set whose files would leave data out (exit 4), writing
nothing. `model lod --inspect BASE.md3` (or `--package P --entry E`) checks an
existing set and exits 4 when the game would read it wrongly; in text mode it
prints the findings before the refusal. See
[detail levels](MODEL_LOD.md).

`model skin-variants SOURCE.mesh.json` lists the named skin variants a source
owns; `--operation add`, `rename --to NEW`, `remove` and `set` need `--name` and
`--output`, and `--surface-material SURFACE=MATERIAL` (repeatable; empty returns
the surface to its primary material) gives surfaces their variant material.
`model skin-export MODEL --output FILE` writes the Quake III `.skin` of the
primary materials or of `--variant NAME`, from an editable source or any native
model; `--format doom3 --variant NAME` writes a Doom 3 skin declaration, with
`--model-path` and `--decl-name`, and exits 4 for a variant a material-to-material
skin cannot express. See [skin variants](MODEL_SKIN_VARIANTS.md).

`model assembly --operation player-review` (and `player-export`) publishes more
skins with `--skin-variants a,b`, `--team-variants` (red and blue),
`--variant-skin NAME:ROLE=PATH` and `--variant-icon NAME=PATH`, detail levels with
`--lod-levels 1-2 --lod-ratio R` or `--lod-recipes`, and custom sounds with
repeated `--sound NAME=PATH`. A PATH starting with `package:` names a package
entry. Review refuses an upper part without `tag_weapon`, a team skin identical to
the model's own materials and any skin that misses a surface (exit 4). See
[native player packages](MODEL_ASSEMBLY.md#native-player-packages).

`model assembly --operation players --package PATH` lists the Quake III players in
a package or folder, with their skins, detail levels, sounds and what stops one
from opening. `model assembly --new --from-player NAME --package PATH --output
FILE.assembly.json` opens one as a new assembly: three package parts on
`tag_torso` and `tag_head`, the `--skin-name` skin (by default `default`) linked
to each part, and native animation from the player's `animation.cfg`. The package
is never written. JSON `player.publication.arguments` holds the `player-review`
options that publish the player again. `animation-set --config-entry PATH` reads
an `animation.cfg` entry from `--package`; add `--config-entry-index N` when the
path is repeated. A missing player, model, `animation.cfg` or complete skin exits
4. See [opening a Quake III player](MODEL_ASSEMBLY.md#opening-a-quake-iii-player).

VibeStudio includes a full command line that runs the same services as the studio. Use it to script
package checks, builds and conversions, to run VibeStudio in CI, or to inspect files without opening
the studio. This page shows how to run it, the options every command shares, and how to use its
output in scripts.

> [!NOTE]
> **Status: Available.** The command line covers every studio area, from projects and packages to
> maps, models, builds and diagnostics, and shares its services and tests with the studio. Like the
> rest of VibeStudio, it has had little real-world use so far.

### Connect map entities

```sh
vibestudio --cli map connect maps/start.map --profile netradiant-custom --object entity:2 --object brush:0 --output maps/linked.map --json
vibestudio --cli map connect maps/start.map --object entity:2 --object entity:7 --kill-target --game quake --output maps/kill-linked.map --dry-run --json
```

`map connect` (alias `map connect-entities`) connects sources to the last selected
entity owner. The default keeps multi-source, destination-first naming. The NRC
profile requires exactly two selected objects and prefers the source's existing
`target` value, then the destination's `targetname`, then a unique classname-based
name. `--kill-target` uses those two-object rules with `killtarget` under every
profile; `--game quake|nexuiz|xonotic` supplies a compatible context. Without
`--game`, built-in map evidence determines the game. Quake II/III and Doom
contexts reject KillConnect. World primitives resolve to worldspawn for NRC
connections. JSON reports `connectionMode`, `profile`, `game`, `property`,
`targetname`, resolved `sourceEntities`, `targetEntity`, and `changedEntities`
(the directly edited entity count, before linked-copy propagation).

Use repeated `--object` selectors, or the existing `--select` spelling, in pick
order; do not mix spellings. `--input` may replace the positional map path.
Unknown options, repeated singleton options, empty values and ambiguous paths
are rejected. `--dry-run` prepares without writing; `--overwrite` authorises an
existing output. Both operations share the GUI's validation, locks and undo
transaction, and preserve the input file.

### Move brushes between map entities

```sh
vibestudio --cli map move-to-entity maps/start.map --object brush:12 --object brush:7 --output maps/moved.map --json
vibestudio --cli map move-to-world maps/start.map --object brush:12 --selected-only --output maps/world.map --json
vibestudio --cli map explode-entity maps/start.map --object entity:4 --rename-targetnames --output maps/pieces.map --json
vibestudio --cli map detail maps/start.map --object brush:12 --structural --policy trenchbroom --output maps/structural.map --json
```

`map move-to-entity` (alias `map move-primitives`) moves the `--object` brushes
and patches, and those of any given brush entity, into an existing entity that
keeps its keys, as **Move Primitives to Entity** does. `--target last` (the
default) and `--target first` choose the entity of the last or first `--object`;
`--target entity:N` names one, worldspawn included. A point entity cannot take
brushes. `map move-to-world --selected-only` moves only the given primitives to
the world, and `map explode-entity` gives every brush and patch of the given
brush entities an entity of its own with identical keys; `--rename-targetnames`
gives each new piece its own `targetname`. `map detail --structural --policy
trenchbroom` also moves the brushes out of brush entities, as TrenchBroom's
**Make Structural** does; `--policy detail` is the default.

Entities left empty go, and primitives keep their ids, groups and visibility.
Locked or linked objects, worldspawn for `explode-entity`, and a move with
nothing to do are refused with exit code 1 and write nothing; a bad `--target`
or `--policy` is a usage error (2). JSON reports `target`, `targetClass`,
`moved`, `movedBrushes`, `movedPatches`, `removedEntities` and `keptHidden`, or
`explodedEntities`, `createdEntities` and `movedPrimitives`, or `policy`,
`movedToWorld` and `flagsCleared`. See
[Edit Quake-family maps](manual/levels.md#edit-quake-family-maps) for the editing workflow.

### Colour map entities

```sh
vibestudio --cli map entity-colour maps/start.map --object entity:3 --object brush:12 --colour "1 0.5 0" --output maps/orange.map --json
vibestudio --cli map entity-colour maps/start.map --object entity:5 --object entity:3 --normalise --output maps/normalised.map --json
vibestudio --cli map entity-colour maps/start.map --where class=light --normalise --each --output maps/lights.map --json
```

`map entity-colour` (alias `map entity-color`) writes the `_color` key of the
`--object` entities and of the entities holding the `--object` brushes and
patches, as **Set Entity Colour** and **Normalise Entity Colour** do. `--colour`
takes three numbers, as 0-1 fractions or 0-255 (spaces or commas), or `#rrggbb`;
a key already written as 0-255 stays 0-255, and a new one is written as
fractions. `--normalise` (or `--normalize`) scales the last object's colour so
its brightest channel is 1, black becoming white, and sets it on every target;
with `--each`, every entity normalises its own colour and ones without a
colour are skipped. `--where` picks the objects by query instead of
`--object`, and `--key` names another colour key, such as `color`.

World brushes add nothing (worldspawn is changed only when named itself), and
hidden entities are left out. A selection with nothing to colour, a last
object without a colour to normalise, and locked content are refused with exit
code 1 and write nothing; giving both or neither of `--colour` and
`--normalise`, or an unreadable colour, is a usage error (2). JSON reports
`mode`, `key`, `value` (what `source`, the target picked last, now holds),
`entities`, `changed`, `skipped`, `worldPrimitives` and `hidden`. An unchanged value
still writes the `--output` copy, with `changed` empty.

### Create map entities by class

```sh
vibestudio --cli map create-entity maps/start.map --class light --origin 64,0,96 --output maps/lit.map --json
vibestudio --cli map create-entity maps/start.map --class info_player_start --definitions defs/game.fgd --ray-origin 32,32,500 --ray-direction 0,0,-1 --view-direction 0,0,-1 --grid 16 --output maps/start-spawn.map --json
vibestudio --cli map create-entity maps/start.map --class func_door --object brush:12 --key speed=100 --output maps/door.map --json
```

`map create-entity` does what a **Create** command on the Levels page does,
such as **Create light**. `--class` must be a point or brush class of the
entity definitions: those of `--definitions` (a file or folder), or else the
built-in classes of the map's game. A point class goes at `--origin x,y,z`
exactly, or is placed against the map as **Paste at Cursor** places objects:
`--camera x,y,z` centres it on a camera, `--plane-point` with `--plane-normal`
and `--ray-direction` stands its definition box on a plane, and `--ray-origin`
with `--ray-direction` and `--view-direction` stands it on the first visible
brush the ray meets (add `--orthographic` for an orthographic view); these
take an optional `--grid`. A brush class makes the `--object` brushes and
patches, with those of given brush entities, into one; an entity left without
brushes goes. Repeat `--key key=value` for more keys, and `--context` names the
layer or group the new entity goes to (`default`, a layer, or a group to open).

The change is one undo step in the editor. A class the definitions lack,
worldspawn, a locked or hidden brush, one outside the open group, or a locked
layer is refused with exit code 4 and writes nothing; options that do not fit
the class, such as `--object` for a point class, are a usage error (2). JSON
reports `className`, `kind`, `entity`, `entityId`, `commandId` (the Levels
command's id), `definitions`, and `origin` and `placement` for a point class
or `brushes` and `patches` for a brush class.

### Expand and move map layers

```sh
vibestudio --cli editor scene select maps/start.map --objects brush:12 --expand-layers --json
vibestudio --cli editor scene move-to-layer maps/start.map --id LAYER-ID --objects brush:12,entity:3 --output maps/layered.map --json
vibestudio --cli editor scene move-to-layer maps/start.map --id default --objects brush:12 --output maps/default.map --dry-run
```

`editor scene select --expand-layers` does what **Expand Selection to Layers**
does on the Levels page: after the `--objects` pick (with `--selection`,
`--mode` and `--context` as usual), it adds every visible, unlocked object in
the layers of the picked objects, nested groups included, and writes nothing.
JSON adds `layers` (layer UUIDs, `default` for the Default layer) and
`skippedGroups`, the closed groups left out because a member is hidden or
locked.

`editor scene move-to-layer` does what **Move to Layer** does: `--id` names a
layer UUID or `default`, and the `--objects` move into it as one undo step. A
member of a closed group moves its whole group, and a brush or patch of a brush
entity moves its entity. It needs `--output`, takes `--dry-run`, and replaces an
existing file only with `--overwrite`. A group's UUID, a locked layer or locked
objects are refused with exit code 4 and write nothing; worldspawn or an unknown
selector is a usage error (2). JSON reports `moved` with `layer`, `objects`,
`released` (brushes that now follow their entity), `groups`, `alreadyThere`,
`worldspawn` and `changed`; objects already in the layer leave the map as it
was. See [Work with layers](manual/levels.md#work-with-layers) for the editing workflow.

### Keep groups for TrenchBroom

```sh
vibestudio --cli editor scene list maps/tb.map --json
vibestudio --cli editor scene save maps/start.map --scene-format trenchbroom --output maps/start-tb.map --json
vibestudio --cli editor scene save maps/start-tb.map --scene-format vibestudio --output maps/start.map --overwrite
```

A map that TrenchBroom saved opens with its layers, groups and linked groups in
the scene, read from its `_tb_` keys, and `editor scene list` reports the scene
with `storage` `trenchbroom`. `editor scene save` writes the map again;
`--scene-format trenchbroom` writes its layers and groups as TrenchBroom's
`func_group` entities too, so TrenchBroom opens it with the same groups and
linked groups, and `--scene-format vibestudio` keeps them in VibeStudio's scene
note alone. Every other writing `editor scene` operation takes the same option;
without it a map keeps the form it opened with. JSON reports the form written as
`sceneStorage`. WADs refuse `trenchbroom` (exit code 2). See
[Share layers and groups with TrenchBroom](manual/levels.md#share-layers-and-groups-with-trenchbroom).

### Replay level edits

For a one-off unique copy or array, use the same operation as **Duplicate, Make
Unique** in the editor:

```sh
vibestudio --cli map duplicate maps/start.map --object brush:0 --object entity:2 --make-unique --delta 64,0,0 --copies 3 --output maps/copied.map --json
```

`--make-unique` clones partial brush/patch entity owners and remaps copied names
and target references together, including outgoing references. It accepts
Quake-family text maps. Each array copy gets fresh names. `map clone` is an alias;
JSON reports `cloneMode` as `make-unique` or `ordinary`.
`--dry-run` validates without writing; existing outputs require `--overwrite`.
See [duplication](manual/levels.md#edit-quake-family-maps) for the editing workflow.

**Copy Repeat Recipe** in Levels exports a recorded sequence for automation:

```sh
vibestudio --cli map repeat maps/start.map --recipe repeat.json --object brush:0 --output maps/repeated.map --json
```

The shared core validates the entire sequence before saving. Repeated `--object`
selectors, group `--context`, `--texture-lock on|off`, `--allow-valve220`,
`--dry-run` and `--overwrite` are supported. JSON map results expose
`map.repeatRecipe`; reopened maps have no session recipe. See
[Repeat object commands](LEVEL_REPEAT.md) for the schema and bounded coverage.

### Run the command line

`model weight-transfer` mirrors or transfers weights between surfaces using
nearest vertices or interpolated triangle samples, exact joint maps, locks and
bounded correspondence. It shares the GUI document service, guarded source
writes and dry-run validation. See [weight transfer](MODEL_WEIGHT_TRANSFER.md)
for all options and examples.

`model weight-paint` stages replace/add/subtract/smooth vertex weights with
joint locks and normalised remainders. Supply explicit surface, joint and
vertices, or replay a versioned coverage recipe; `--output .mesh.json` is
required. See [weight painting](MODEL_WEIGHT_PAINT.md) for options, examples
and atomic validation rules.

`model skinning` inspects influences, selects vertices by weight or issue, and
binds, rebinds, reduces or removes skinning. It shares the GUI review service,
guarded source outputs, dry-run and JSON diagnostics. See [skin weights and
binding](MODEL_SKINNING.md#automate-the-workflow) for options and examples.

`model rig` inspects and authors skeleton joints and selected vertex weights.
It supports `list`, `add`, `rename`, `reparent`, `remove` and `weight`, with
guarded `.mesh.json` outputs, `--dry-run`, `--overwrite` and JSON reports.
See [skeleton authoring](MODEL_RIG.md#automate-the-workflow) for options and
examples shared with **Edit Skeleton** in the Mesh Editor.

`model pose` manages skeletal clips, transforms bind joints and pose ranges,
copies joint poses and interpolates hierarchical motion. Both commands share
the modeller's document service, guarded outputs and dry-run validation. See
[skeletal animation authoring](MODEL_RIG.md#author-skeletal-animation).
`model pose --operation working-set --clips a,b` chooses the clips baked into
editable frames (`-` keeps the bind pose alone; without `--clips` they are fitted
automatically), and its listing reports the working set. `model import` re-bakes
a skeletal model over the editable frame budget, reports `rebaked` and
`workingSet` in JSON, and takes the same `--clips` to choose instead. In both
commands a clip name or index that matches no clip, or names one twice, exits 2;
a choice over the budget exits 4. A model named relative to the working folder
still finds its companion files (md5anim, GLA, `.def`) beside it. See
[the skeletal working set](MODEL_SKELETAL_WORKING_SET.md#use-the-command-line).

```sh
vibestudio --cli model import ./models/monster.md5mesh --clips idle,walk --output ./out/monster.mesh.json --json
vibestudio --cli model pose ./out/monster.mesh.json --operation working-set --clips idle,run --output ./out/run.mesh.json
```
`model import` and `model edit` report the source they wrote (or would write
with `--dry-run`) in JSON as `"source": {"bytes", "encoding", "limitBytes"}`.
The encoding is `readable`, or `packed` when the readable source would pass the
64 MiB limit and the bulk arrays were packed (source version 12); text output
then adds a line saying so. The skeletal writers (`model pose`, `model rig`,
`model skinning`, `model ik`, `model curves`, `model curve-keys`,
`model curve-transfer`, `model weight-paint`, `model weight-transfer` and
`model skin`) report the same object as `"outputSource"`, with the same text
line for a packed source. An edit that would take a source over the limit
exits 4 with its size and writes nothing. See
[Editable Meshes](MODEL_MESH.md#sources-import-and-export).
Joint transforms also accept an exclusive `--matrix` with twelve row-major
3×4 affine values, shared with the viewport handles. See
[viewport posing](MODEL_RIG.md#pose-joints-in-the-viewport) for its contract.

The command line is the studio program itself, started with `--cli`:

```text
vibestudio --cli [--json] <family> <command> [arguments]
```

| How you got VibeStudio | Program to run |
| --- | --- |
| Windows installer or portable zip | `vibestudio.exe` in the `bin` folder where you installed or extracted VibeStudio |
| macOS disk image | `/Applications/VibeStudio.app/Contents/MacOS/vibestudio` |
| Linux AppImage | `./VibeStudio-<version>-linux-x86_64.AppImage` |
| Nightly package on macOS or Linux | `bin/vibestudio` in the folder you extracted |
| Built from source | `builddir/src/vibestudio`, or `builddir\src\vibestudio.exe` on Windows; see [Build from source](manual/building-from-source.md) |

The examples in this manual write `vibestudio`. Replace it with the program's path, or add its folder
to your PATH. For example:

```sh
/Applications/VibeStudio.app/Contents/MacOS/vibestudio --cli --version
./VibeStudio-<version>-linux-x86_64.AppImage --cli package info ./id1/pak0.pak
```

`vibestudio --cli` on its own, or with `--help`, prints the full help: the shared options, then every
command with an example. Each command there lists the options it supports in brackets, such as
`[--json --dry-run]`. Quote paths that contain spaces, as in PowerShell:

```powershell
vibestudio --cli package validate "C:\Games\Quake\id1\pak0.pak" --json
```

### Options every command shares

Options can go anywhere after `--cli`.

| Option | What it does |
| --- | --- |
| `--help` | Prints the full help and exits. |
| `--version` | Prints the version and exits. |
| `--json` | Prints one JSON object instead of text. Every command supports it. |
| `--quiet` | Hides the text output of a successful command; errors still go to standard error. Ignored with `--json`. |
| `--verbose` | Adds timing and extra diagnostics to text output. Ignored with `--json`. |
| `--settings-file <path>` | Reads and writes settings in this INI file instead of your own settings. |
| `--dry-run` | Shows what a command that writes files would do, without writing. |
| `--overwrite` | Lets a command replace an existing output file. |
| `--watch` | Streams progress lines from `build run`, `compiler run` and `compiler rerun` in text output. Nothing is streamed with `--json`. |
| `--task-state` | Adds task-state objects to JSON output where supported. |
| `--exit-codes` | Prints the exit-code table below. |
| `--renderer <automatic\|opengl\|vulkan>` | Chooses the 3D renderer for this run (`material render`, `render backends`, `render test`) without changing the setting. |

The command line uses the same settings as the studio, so commands such as `install add`,
`compiler set-path` or `editor select` change what the studio sees. In scripts and CI, pass
`--settings-file` with a file of your own to keep them apart. `vibestudio --cli --settings-report`
prints where the settings in use are stored.

### Command families

Commands are grouped into families. `vibestudio --cli cli commands` prints the complete, current
list, and `--json` adds whether each command supports `--json`, `--dry-run` and `--watch`.

| Family | Covers |
| --- | --- |
| `cli` | The command list and the exit-code table |
| `ui` | Status chips, default keyboard shortcuts and command palette entries |
| `project` | Project manifests, health checks and file lists |
| `install` | Game installation profiles, Steam and GOG detection, and the index of each game's own assets |
| `workspace` | `.vibeworkspace` files |
| `package` | Inspecting, extracting, validating, comparing, staging and saving packages, drafts and recovery |
| `release` | Packaging and releasing a project, map, model or textures: plans, release notes, the changelog, publishing and release history |
| `asset` | Format capabilities, image conversion, audio editing, and project text search and replace |
| `map` | Inspecting, editing, rendering and generating maps, their textures and dependencies |
| `entity` | Entity definition catalogues and map entity validation |
| `editor` | Editor profiles, gestures, keys, view layout and plan panes, saved views and map layers |
| `model` | Inspecting, importing, editing, building and exporting models |
| `texture` | Decoding, creating, editing, exporting, staging and generating textures |
| `shader` | Quake III shader scripts |
| `material` | Textures, shaders and materials of idTech 1 to 4: list, check, render, edit |
| `render` | The 3D renderers: what each one found, a drawing test, and which one to use |
| `sprite` | Doom and Quake sprite plans |
| `code` | Code file lists, local language servers and format-preserving text saves |
| `compiler` | Compiler discovery, paths, single compiler runs and command manifests |
| `build` | Build pipelines and prepared builds |
| `bsp` | Compiled map inspection |
| `launch` | Game launch plans and launching |
| `localization` | Language targets and translation catalogue status |
| `accessibility` | Accessibility settings, speech voices and a speech test |
| `diagnostics` | Support bundles and crash reports |
| `ai` | Optional AI: status, connectors, connection tests, questions (`ai ask`), pictures (`ai image`) and rule-based proposals |
| `extension` | Extension manifests |
| `about`, `credits` | Version and credits, and credits validation |

### Common recipes

List every command:

```sh
vibestudio --cli cli commands
```

Inspect, filter and validate a package:

```sh
vibestudio --cli package info ./id1/pak0.pak
vibestudio --cli package list ./id1/pak0.pak --where "ext=wav"
vibestudio --cli package validate ./release.pk3 --json
```

Render a map to an SVG picture, optionally with a compiler leak trail. The projection is `top`,
`front` or `side`; without `--output` the SVG goes to standard output. `--grid` sets the grid spacing in map
units, fractions such as `0.5` included; a grid too dense for the picture is left out with a warning.
`--block-grid` adds the compiler block grid as dashed lines, from the worldspawn `_blocksize` key (1024 units
when it is missing; 0 leaves an axis unsplit), as the Levels views draw it with **Show Block Grid**; Doom maps have
none and say so in a warning. With `--json` the report's `blockGrid` gives the `size` per axis, the `key` and
`value` it came from, how many numbers the compiler reads from it (`numbersRead`) and the `drawnLines`.

```sh
vibestudio --cli map render ./maps/start.map --output ./start.svg
vibestudio --cli map render ./maps/start.map --projection front --leak ./maps/start.pts --output ./start-leak.svg --overwrite
vibestudio --cli map render ./maps/arena.map --block-grid --projection front --output ./arena-blocks.svg
```

Index a game's own files once, then review and publish a map release with only your own files:

```sh
vibestudio --cli install register build quake3-games-quake3
vibestudio --cli release plan ./mymod --map maps/arena1.map
vibestudio --cli release publish ./mymod --map maps/arena1.map --release-version 1.0.0
```

Plan a build without running anything, then run it with live progress:

```sh
vibestudio --cli build list
vibestudio --cli build plan quake-full --input ./maps/start.map
vibestudio --cli build run quake-full --input ./maps/start.map --watch
```

Ask a model through your Claude Code login, or build a level with Quake-MapGen:

```sh
vibestudio --cli ai test-connection --provider claude-code --json
vibestudio --cli ai ask --provider claude-code --model sonnet --prompt "why does qbsp report a leak?" --context-file build/qbsp.log --yes
vibestudio --cli map generate --generator quake-mapgen --prompt "Quake 2 deathmatch, 8 rooms" --output maps/qmg.map --report maps/qmg.json
```

`--provider` takes a connector ID from the table in [Use a cloud provider](manual/ai.md#use-a-cloud-provider),
such as `openai`, `claude`, `gemini`, `elevenlabs`, `local-offline` or `custom-http`; text commands
also take `claude-code`, `codex`, `custom-command` and `local-gguf`. For `claude-code`, `codex` and
`custom-command`, `--endpoint` is the program or command line; for `local-gguf`, `--model` is the
`.gguf` file. Map generation adds `--generator quake-mapgen` for Quake-MapGen, and `--ranker ggml`
ranks its layouts through ggml in builds made with `-Dggml=enabled`; the default, `scalar`, gives the
same scores. AI is off until you allow it; see [AI assistant](manual/ai.md).

Write a diagnostics bundle to `./diagnostics/vibestudio-diagnostics.json` for a bug report:

```sh
vibestudio --cli diagnostics bundle --output ./diagnostics
```

List the target languages, then report on one of them:

```sh
vibestudio --cli localization targets
vibestudio --cli localization report --locale ar --json
```

Print the status chips with their non-colour signals, the default shortcuts with any conflicts, and
the command palette entries:

```sh
vibestudio --cli ui semantics
```

More examples are on the pages for each area, such as [Packages](manual/packages.md),
[Package and release](manual/releases.md) and [Build and launch](manual/build-and-launch.md).

### Use JSON output in scripts

With `--json`, a command prints one JSON object on standard output, even when it fails. Every object
has the same core fields, plus fields of its own:

| Field | Meaning |
| --- | --- |
| `command` | The command that ran, such as `package info`. |
| `ok` | `true` only when the exit code is 0. |
| `status` | `success` or `error`. |
| `exitCode` | The exit code as an object with `code`, `id`, `label` and `description`. |
| `message` | What went wrong, when the command failed. |

For example, a package that does not exist:

```json
{
    "command": "package info",
    "exitCode": {
        "code": 1,
        "description": "The command was understood but the operation failed.",
        "id": "failure",
        "label": "Operation failure"
    },
    "message": "Unable to open package: Package path does not exist.",
    "ok": false,
    "status": "error"
}
```

Without `--json`, results go to standard output and errors to standard error.

### Exit codes

| Code | ID | Meaning |
| --- | --- | --- |
| 0 | `success` | The command completed successfully. |
| 1 | `failure` | The command was understood but the operation failed. |
| 2 | `usage-error` | Arguments were missing, malformed or incompatible. |
| 3 | `not-found` | A requested project, package, entry, installation or tool was not found. |
| 4 | `validation-failed` | Validation completed and found blocking problems. |
| 5 | `unavailable` | The workflow is recognised but no capable implementation or tool is available yet. |

A check that finds a problem exits with 4, not 1, so a script can tell "the check failed" from "the
command failed". For example, `package validate`, `package compare` and `entity validate` exit with 4
when they find problems or differences:

```sh
vibestudio --cli package validate ./release.pk3 --json > validation.json
if [ $? -eq 4 ]; then echo "release.pk3 has problems"; fi
```

```powershell
vibestudio --cli package validate .\release.pk3 --json | Out-File validation.json
if ($LASTEXITCODE -eq 4) { Write-Host "release.pk3 has problems" }
```

<details>
<summary>Single-option reports</summary>

These options print a report and exit:

| Option | Prints |
| --- | --- |
| `--platform-report` | Platform and Qt runtime details |
| `--settings-report` | Where settings are stored, and recent projects |
| `--preferences-report` | Accessibility and language preferences |
| `--setup-report` | First-run setup status |
| `--compiler-registry` | Compiler discovery, the same as `compiler list` |
| `--localization-report` | Localization targets and catalogue status |

</details>

### Learn more

- [CLI strategy](CLI_STRATEGY.md): the full reference for every family, option and output field.
- [Troubleshooting and FAQ](manual/troubleshooting.md): logs, diagnostics bundles and crash reports.


`map copy-surface --format map` exports ordered `.map` face records from repeated
`--target face:brushId:faceNumber` options. `paste-surface --clipboard faces.map`
auto-detects them and defaults to `--mode attributes`: last face supplies values,
target contents remain, and absent source surface flags/value preserve the target.
The default JSON copy and parameter paste remain available. See
[face clipboard examples](LEVEL_PASTE.md#cli) for output protections and limits.

`map copy <map> --object brush:0 --output fragment.map` exports selected objects
as `.map` text. `map cut <map> --object entity:1 --clipboard fragment.map --output
remaining.map` exports them and saves the remaining map. Repeat `--object` for
brushes, patches or entities. Create output directories first; existing files
require `--overwrite`, including dry runs. Cut JSON reports `clipboardWritten`
and `mapWritten` separately, so a saved fragment is recoverable if the map save
fails. See [object clipboard examples](LEVEL_PASTE.md#cli).


### Select level objects by hierarchy or source line

`map select-siblings <map> --object brush:12` returns selectable siblings;
`map select-lines <map> --line "42,57"` finds objects in the loaded source text.
Repeat **--object** or **--line** for multiple inputs. Both accept **--context**
for an open group and repeated **--exclude** selectors for external filters.
They are read-only queries: JSON includes `selection`, `sourceSha256`,
`queryOnly: true` and `mapWritten: false`. Pass returned selectors to normal map
authoring commands. See [Select by hierarchy or source line](manual/levels.md#select-by-hierarchy-or-source-line)
for ownership, group, source-snapshot and undo behaviour.

### Query level objects and faces by type

`map select-type <map> --object brush:12` matches every selected owner's exact
class. With no non-world owner, supply **--material** to query matching brushes
and patches. **--mode faces --object entity:2 --material stone** returns matching
faces while retaining object selectors. **vertices** and **edges** modes are
no-ops. Repeated **--exclude** and **--context** supply view/group boundaries.
The read-only JSON response includes `matchedClasses`, `materialFallback`,
`componentSelectionChanged`, component `sets`, reusable face `selectors` and
one-based `surfaceTargets` for **map copy-surface**, **map paste-surface** and
material commands. Pass each surface selector as **--target**; component
selectors remain zero-based and use the component-transform syntax.
See [Select all of a type](manual/levels.md#select-all-of-a-type) for exact matching,
material fallback, ownership, visibility and history rules.

### Query textured level surfaces

`map select-textured <map> --material stone` adds matching brush faces as
independent surface picks and matching patches as whole object picks. Repeated
**--object** and **--face face:brushId:faceNumber** supply existing picks to
preserve; face numbers are one-based. The query does not add parent brushes,
change component handles or replace other picks. Matching ignores case and
preserves whitespace. A no-match result leaves the supplied picks unchanged.

New matches respect hidden objects, locks, repeated **--exclude** filters and
the **--context** open-group boundary. Closed groups must be opened first.
Existing hidden picks remain in the response; check their editability before
using them for authoring. The combined face and matching patch result is limited
to 16,384 targets; overflow or invalid brush geometry fails the complete query.

JSON includes `faces`, `matchedPatches`, `selection`, `originalSelection`,
`addedFaces`, `addedPatches` and `surfaceTargets`. Pass each returned surface
target to **map paint-material --target**, **map copy-surface --target** or a
compatible **map paste-surface --target** workflow. `surfaceTargets` includes
the independent face picks and matching patches, without expanding unrelated
object picks. The response also includes `sourceSha256` and
`brushGeometrySha256`: re-query after source or geometry changes rather than
reusing stale face indices. Queries do not write a map or package draft.

**--within-selection** limits new matches to the **--object** objects (their
brushes and patches, and those of an entity), as the **Find** button of
**Find and Replace Textures…** does for **Selected objects**; JSON then reports
`withinSelection: true`.

**Status: Partial.** The shared query, the CLI and the **Select Textured**
viewport action share one service. See
[Select textured surfaces](manual/levels.md#select-textured-surfaces).

### Find and replace level textures

`map replace-texture <map> --from base/wall --to base/metal --output out.map`
replaces every use of a texture, compared without case, on brush faces,
patches, or Doom wall textures and flats, as one edit. Repeated
**--object kind:id** limits it to those objects; repeated
**--face face:brushId:faceNumber** (one-based, as **map select-textured**
prints it) limits it to those faces instead, with **--context GROUP** to open a
group first. The two cannot be combined.

```sh
vibestudio --cli map select-textured room.map --material base/wall --json
vibestudio --cli map replace-texture room.map --from base/wall --to base/metal --face face:6:5 --face face:9:5 --output room-metal.map
```

Hidden and locked objects, and anything outside **--context**, keep their
textures; they are counted in `skipped` instead of failing the command. JSON
adds `replaced`, `skipped`, `scope` (`map`, `selection` or `picks`) and
`targets`, the surfaces in scope that used the texture. A missing face, a
`--face` that is not a brush face, or nothing in scope using the texture fails
without writing. **Find and Replace Textures…** in Levels uses the same
service; see [Find, replace and reveal textures](manual/levels.md#find-replace-and-reveal-textures).

### Move, rotate and scale textured faces

Pass the `faces` from **map select-textured** to **map transform-components**
with repeated **--face face:brushId:faceNumber** to transform those faces
without selecting their brushes:

```sh
vibestudio --cli map select-textured room.map --material stone --json
vibestudio --cli map transform-components room.map --face face:6:5 --face face:9:5 --operation move --delta 0,0,16 --uv-lock on --output raised.map
```

**--face** can be combined with **--set** in the same edit and accepts the same
`move`, `rotate`, `scale` and `shear` options; `snap` refuses it.
**--context GROUP** opens a group before the picks are checked. A pick on a
hidden, locked or closed-group brush, a missing face, or picks on more than 256
brushes fail with exit code 2 and write nothing. JSON adds `faces` (the picks
after the edit, which may have new face numbers), `brushGeometrySha256`,
`retiredFaces` and `parentBrushesSelected: false`. Pass the new `faces` to the
next command to keep working on the same surfaces. See
[Transform independently picked faces](manual/levels.md#transform-independently-picked-faces).

### Step textures by Radiant increments

`map align-textures` takes NetRadiant Custom's Surface Inspector steps.
**--scale-step U,V** adds each increment to the texture scale of every selected
face, keeping the texture origin in place; a scale that would reach zero flips
its sign instead, and `0` leaves that axis alone. **--degrees N** turns about
each face centre, or about the texture origin with **--anchor origin**:

```sh
vibestudio --cli map align-textures room.map --object brush:12 --scale-step -0.5,0 --output narrower.map --json
vibestudio --cli map align-textures room.map --face 12:3 --degrees -45 --anchor origin --output turned.map
```

Brush-primitive faces need **--package** or **--texture-size** for a scale step,
as patches do for a turn. Patches multiply their coordinates by a positive step
and divide them by a negative one. **--anchor** goes only with **--degrees**.
JSON reports `operation` (`scale-step` or `degrees`) and, for turns, `anchor`
(`centre` or `origin`). `editor controls <profile>` reports the profile's
`surfaceRotationStep` and `surfaceRotationPivot` (`face-centre` or
`texture-origin`). See
[Step textures by Radiant increments](manual/levels.md#step-textures-by-radiant-increments).

### Move and flip textures as a camera sees them

`map align-textures` also takes the Levels camera's texture moves and flips,
for a camera looking along **--camera-yaw** degrees (0 along +X, turning
anticlockwise) and **--camera-pitch** degrees (up positive, -90 to 90), both 0
when left out. **--camera-shift RIGHT,UP** moves each selected face's texture
RIGHT texels across the view and UP texels up it, along whichever of the
face's texture axes runs most nearly that way. **--camera-flip horizontal** or
**vertical** mirrors the axis running across or up the view, about the middle
of each face:

```sh
vibestudio --cli map align-textures room.map --face 12:3 --camera-shift 0,16 --camera-yaw 90 --camera-pitch -20 --output moved.map --json
vibestudio --cli map align-textures room.map --object brush:12 --camera-flip horizontal --camera-yaw 45 --output flipped.map
```

Brush-primitive faces and patches need **--package** or **--texture-size** for
a move; flips need no image. **--camera-yaw** and **--camera-pitch** go only
with these two operations. JSON reports `operation` (`camera-shift` or
`camera-flip`), `cameraYaw` and `cameraPitch`. See
[Move and flip textures from the camera](manual/levels.md#move-and-flip-textures-from-the-camera).

### Turn textures clockwise by fixed steps

`map align-textures --clockwise N` turns each selected face's texture N
degrees clockwise as seen from the face's front, about the texture origin, as
the Levels **Rotate Textures** commands do. A negative N turns
counter-clockwise. The direction holds on classic, Valve 220 and
brush-primitive faces and on patches, whether the texture reads the right way
round or mirrored:

```sh
vibestudio --cli map align-textures room.map --object brush:12 --clockwise 15 --output turned.map --json
vibestudio --cli map align-textures room.map --face 12:3 --clockwise -90 --output turned.map
```

Patches need **--package** or **--texture-size**; faces need no image.
**--anchor** and the camera options do not go with **--clockwise**. JSON
reports `operation` `clockwise`, `anchor` `origin` and `clockwiseDegrees`. See
[Rotate textures by fixed steps](manual/levels.md#rotate-textures-by-fixed-steps).

### Reset textures

`map align-textures` resets every selected face in one pass, whatever its
format, at U,V map units per texel with no shift or rotation.
**--reset-alignment U,V** keeps each face's kind, as the Levels **Reset Texture
Alignment** command does: classic faces natural, Valve 220 and brush-primitive
faces in their own plane, patches their natural spread. **--radiant-reset U,V**
resets as **Radiant Texture Reset** does: Valve 220 and brush-primitive faces
take their plane's own basis and patches are capped:

```sh
vibestudio --cli map align-textures room.map --object brush:12 --reset-alignment 1,1 --output reset.map --json
vibestudio --cli map align-textures room.map --object entity:4 --radiant-reset 0.5,0.5 --package ./assets --output reset.map
```

Instead of an operation, Reset Textures values set absolute numbers, as the
Levels **Reset Textures…** window does: **--set-scale-u N**, **--set-scale-v N**,
**--set-shift-u N**, **--set-shift-v N** and **--set-rotation N** for each value
to set, and **--rename NAME** for a new texture. They act on the **--object**
and **--face** selection, or on the **--whole-map** (its visible, unlocked
surfaces in the open group), and **--only-texture NAME** keeps them to the
surfaces using one texture, ignoring case and a leading `textures/`. Patches
take only the new name:

```sh
vibestudio --cli map align-textures room.map --whole-map --only-texture base_wall/concrete --rename base_wall/metal --set-scale-u 0.5 --set-scale-v 0.5 --output retextured.map --json
```

Brush-primitive faces need **--package** or **--texture-size** for a reset or
for values, as patches do for a reset. A scale of 0, a texture name with spaces
or quotes, and a request that sets nothing are refused. JSON reports
`operation` (`reset-alignment`, `radiant-reset` or `reset-textures`), and for
Reset Textures `scope` (`selection` or `map`), `targetFaces`, `targetPatches`,
`renamed` and the `set` values. See
[Reset textures](manual/levels.md#reset-textures).

### Fit and project textures

**--fit U,V --keep-proportions**, with exactly one of U and V set to 0, fits
the other axis and scales the 0 axis by the same factor, starting it at the
face's edge, so the texture keeps its proportions, as the Levels **Fit Texture
Width** and **Fit Texture Height** commands do. Without
**--keep-proportions** a 0 axis is left as it is.

**--project** lays the default texture flat over each selected face and patch,
with no shift or rotation, at the map's default scale (0.5 for Quake III, 1
otherwise) or **--project-scale U,V** map units per texel. **--project axial**
projects each face along the world axis it looks down most and each patch
along the axis nearest its facing. **--project x**, **y**, **z**, **-x**,
**-y** or **-z** projects along that world axis, pointing out of the faces that
show the texture the right way round, as a 2D view does from its side (Top
`z`, Front `-y`, Side `x`). **--project camera** projects back towards a
camera at **--camera-yaw** and **--camera-pitch**, and
**--project-direction X,Y,Z** along any direction:

```sh
vibestudio --cli map align-textures room.map --face 12:3 --fit 2,0 --keep-proportions --package ./assets --output fitted.map
vibestudio --cli map align-textures room.map --object brush:12 --project z --output projected.map --json
vibestudio --cli map align-textures room.map --object brush:12 --project camera --camera-yaw 45 --camera-pitch -30 --output projected.map
```

Faces the direction runs along, and patches it flattens to a line, keep their
mapping, as do classic faces whose format cannot hold the projection; a
request that would change nothing else is refused. Brush-primitive faces and
patches need **--package** or **--texture-size** for a projection, and every
face but a brush-primitive one needs it for a fit. JSON reports
`keepProportions` for a proportional fit, and for a projection `projection`,
`scale`, the unit `direction`, `skippedEdgeOn` and `skippedClassic`. See
[Fit and project textures](manual/levels.md#fit-and-project-textures).

### Read and set face flags

`map face-flags` reads or changes the content flags, surface flags and value
of Quake II and Quake III brush faces, as the **Flags** section of the Levels
**Surfaces** tab does. Name faces with **--object brush:ID** or **entity:ID**
and **--face brushId:faceNumber** (face numbers start at one); both repeat.

Without a change, it reports each face's flags by name, and with no faces the
names the map's game gives its flags:

```sh
vibestudio --cli map face-flags base.map --face 3:1 --json
vibestudio --cli map face-flags base.map --json
```

To change them, give **--set-content**, **--clear-content**, **--set-surface**
or **--clear-surface** with comma-separated flag names or numbers (decimal or
**0x** hex), **--value N** for the surface value, or **--clear-all** to clear
every flag but detail and set the value to 0, as NetRadiant Custom's **Unset**
does. Bits you do not name stay as they are, so a change works on faces whose
flags differ. A change needs **--output**:

```sh
vibestudio --cli map face-flags base.map --object brush:3 --set-content playerclip --clear-surface light --output base-clip.map
vibestudio --cli map face-flags base.map --face 3:2 --set-surface light --value 300 --output base-light.map --json
```

The detail flag belongs to whole brushes, so use `map detail` for it. Quake
maps, hidden faces, locked layers and faces outside the **--context** group are
refused and nothing is written. A changed flag word is written as a signed
32-bit number, so the top bit (`n64_scroll_flip`, `nodrop`) appears as a
negative number that every compiler reads. The change is one undo step and supports
**--dry-run** and **--overwrite**. JSON reports `game`, `changedFaces`,
`brushes`, the bits set and cleared by name and the `value`. See
[Set face flags](manual/levels.md#set-face-flags).

### List and edit texture coordinates

`map edit-uv` works on one brush face's outline or one patch's control points
in texture space, as the Levels **UV Editor** does. Name the surface with
**--target face:brushId:faceNumber** (face numbers start at one) or
**--target patch:id**. Coordinates are in texels, U to the right and V down
the image.

Without a change, it lists each point's number, world position and texel
coordinates (and repeats, when the image size is known):

```sh
vibestudio --cli map edit-uv base.map --target face:3:1 --json
```

Give one change and **--output**:

- **--move-point N --to U,V** moves a face's whole texture so point N reads
  U,V, or sets patch control point N to U,V.
- **--offset U,V** adds to every coordinate; on a patch, **--points 1,4,7**
  chooses the points.
- **--rotate DEG** turns clockwise as the **UV Editor** draws texture space
  (negative turns anticlockwise), about **--anchor N** or else the centre of
  the points that turn.

```sh
vibestudio --cli map edit-uv base.map --target face:3:1 --rotate 15 --anchor 1 --output base-turned.map
vibestudio --cli map edit-uv curves.map --target patch:0 --move-point 5 --to 32,16 --texture-size 128,128 --output curves-uv.map --json
```

Brush-primitive faces and patches count repeats, so they need **--package**
(with **--palette** where the game uses one) or **--texture-size W,H**. A
classic face that cannot hold a turn exactly (its texture is scaled
differently along U and V) takes the nearest classic mapping, keeping the
anchor, unless **--allow-valve220** converts it to Valve 220. Hidden faces and
patches, locked layers and targets outside the **--context** group are refused
and nothing is written. The change is one undo step and supports **--dry-run**
and **--overwrite**. JSON reports `format`, `material`, `imageSize`, `points`
(after the change), `fit` (`exact`, `converted`, `approximated` or
`unchanged`) and, for a turn, the `pivot`. See
[Edit texture coordinates in the UV Editor](manual/levels.md#edit-texture-coordinates-in-the-uv-editor).

### Round level geometry to the grid

`map round-to-grid` rounds each brush vertex, patch control point and
point-entity origin of the **--object** objects onto **--grid**, as Radiant's
Snap To Grid and the Levels page's **Round Selection to Grid** do. Brush
entities round their brushes and patches and keep their origin key. Repeat
**--set** to round only picked components instead:

```sh
vibestudio --cli map round-to-grid room.map --object brush:12 --object entity:3 --grid 8 --output rounded.map --json
vibestudio --cli map round-to-grid room.map --set brush:12/vertex/0,3 --grid 16 --uv-lock fit --output rounded.map
```

Hidden objects and brushes the grid would collapse are left as they were and
listed under `skipped`; when every object is refused the command fails and
writes nothing. **--set** components are all or nothing, and
**--allow-collapse** lets their corners merge. **--uv-lock off|on|fit**,
**--allow-valve220** and **--context GROUP** behave as they do for
**map transform-components**: `on` keeps every corner's texture coordinates or
refuses, `fit` keeps three corners of a face that cannot hold them all. JSON
reports `brushes`, `patches`, `entities`, `collapsedVertices`, `fittedFaces`,
`skipped` and the save fields.

**Status: Partial.** The shared service, the CLI and the Levels command share
one code path. See [Edit Quake-family maps](manual/levels.md#edit-quake-family-maps).

### Nudge level objects and components

`map nudge` is the command line form of the Levels page's **Nudge** commands. It
moves the **--object** objects, or the repeated **--set** components, by
**--steps** grid steps of **--grid** units (1 and 16 by default) towards
**--direction**: `left`, `right`, `forward`, `back`, `depth-up`, `depth-down`,
`z-up` or `z-down`. The direction resolves as it does in a view: **--view top**,
**front** or **side** names a plan view (top by default), and
**--camera-yaw** with **--camera-pitch** names a camera looking that way in
degrees, yaw 0 along +X and counter-clockwise, pitch up positive:

```sh
vibestudio --cli map nudge room.map --object brush:12 --direction depth-up --view front --grid 8 --output nudged.map --json
vibestudio --cli map nudge room.map --set brush:0/face/2 --direction forward --camera-yaw 60 --uv-lock on --output nudged.map
```

In the front view `depth-up` is towards the viewer, -Y; in the camera it is up
world Z, and `forward` follows the horizontal world axis nearest the view.
Objects take **--texture-lock on|off** (on by default); components take
**--uv-lock off|on|fit** and **--allow-collapse**, as **map transform-components**
does, with **--allow-valve220** and **--context GROUP**. Doom maps nudge along X
and Y only. A locked object or a usage error fails and writes nothing. JSON
reports `direction`, `frame`, `axis`, `delta`, `grid`, `steps`, `mode`, the
counts and the save fields.

**Status: Partial.** The axes, the CLI and the Levels commands share one core
service. See [Nudge the selection](manual/levels.md#nudge-the-selection).

Add **--duplicate** to copy the **--object** selection and move the copies
instead, as the Levels **Duplicate and Move** commands do. JSON adds
`duplicate` and the `copies`:

```sh
vibestudio --cli map nudge room.map --object brush:12 --direction right --duplicate --grid 32 --output copied.map --json
```

See [Copy and move in one step](manual/levels.md#copy-and-move-in-one-step).

### Move level objects to the camera

`map move` with **--camera x,y,z** is the command line form of **Move to
Camera**. It centres the repeated **--object** selection on that point by the
**Paste to Camera** translation, snapped to **--grid** (0, no snapping, by
default), and keeps the objects' identities and place in the file. Point
entities are sized from **--definitions** when you give one, otherwise as a
16-unit marker. **--texture-lock on|off**, **--allow-valve220** and
**--context GROUP** work as for other moves; Quake-family maps only. A selection
already at the camera fails and writes nothing. JSON reports `camera`, `delta`,
`grid` and the save fields:

```sh
vibestudio --cli map move room.map --object brush:3 --object entity:2 --camera 128,0,64 --grid 16 --output at-camera.map --json
```

### Repeat transforms on copies or components

`map repeat-transforms` is the command line form of **Repeat Transforms**. Name
the transforms to repeat: **--scale x,y,z** (a negative factor mirrors),
**--shear AXIS,ALONG,FACTOR**, **--rotate AXIS,DEGREES** and **--translate
x,y,z**; they apply in that order. With **--object**, the selection is copied
where it stands and the copies are scaled, sheared and turned about their
centre, or **--pivot x,y,z**, then moved. With repeated **--set** components
they are transformed in place instead, with **--uv-lock off|on|fit** and
**--allow-collapse**:

```sh
vibestudio --cli map repeat-transforms room.map --object brush:4 --rotate z,15 --translate 64,0,0 --output array.map --json
vibestudio --cli map repeat-transforms room.map --set brush:0/face/2 --translate 0,0,8 --uv-lock on --output raised.map
```

Objects take **--texture-lock on|off** (on by default). Quake-family maps only.
One undo step; a locked object or a usage error fails and writes nothing. JSON
reports `transforms`, `mode`, `pivot`, the `copies` or component counts, and
the save fields.

**Status: Partial.** The CLI and the Levels commands share one core service.
See [Repeat the last transforms](manual/levels.md#repeat-the-last-transforms).

### Turn and flip level objects as a view shows them

`map view-transform` is the command line form of the Levels page's
view-relative commands: **Roll Clockwise** and **Counter-clockwise**, **Rotate
90° Right** and **Left** (yaw), **Pitch Clockwise** and **Counter-clockwise**,
**Flip Horizontal** and **Flip Vertical**. Name one with **--transform**:
`roll-clockwise`, `roll-counterclockwise`, `yaw-clockwise`,
`yaw-counterclockwise`, `pitch-clockwise`, `pitch-counterclockwise`,
`flip-horizontal` or `flip-vertical`. The axis resolves as it does in a view:
**--view top**, **front** or **side** names a plan view (top by default), and
**--camera-yaw** with **--camera-pitch** names a camera, as for `map nudge`:

```sh
vibestudio --cli map view-transform room.map --object brush:12 --transform roll-clockwise --view front --pivot-grid 16 --output rolled.map --json
vibestudio --cli map view-transform room.map --object entity:3 --transform flip-horizontal --camera-yaw 60 --camera-pitch -20 --output flipped.map
```

From the front view, `roll-clockwise` turns +90° about Z; from a camera looking
nearest +Y, `flip-horizontal` mirrors along X. The objects turn or flip about
the exact centre of what moves, about **--pivot x,y,z**, or about that centre
rounded with **--pivot-grid N**: to N for turns and to N/2 for flips, as the
TrenchBroom profile does while **Snap** is on. **--texture-lock on|off** (on by
default), **--allow-valve220** and **--context GROUP** work as for `map rotate`.
Doom maps flip along X or Y and turn about Z only, so roll and pitch fail there.
A locked object or a usage error fails and writes nothing. JSON reports
`transform`, `commandId`, `frame`, `axis`, `degrees`, `mirror`, `pivotPolicy`,
`pivot` and the save fields. To change picked components, use
`map transform-components`.

**Status: Partial.** The axes, the CLI and the Levels commands share one core
service. See [Turn and flip from the view](manual/levels.md#turn-and-flip-from-the-view).

### Flip and turn level objects on world axes

`map flip --axis x|y|z` and `map rotate --axis x|y|z --turns N` are the command
line form of the Levels page's **Flip X/Y/Z** and **Rotate X/Y/Z**. Rotate X and
Rotate Z are `--turns -1`, Rotate Y is `--turns 1`. **--pivot-grid** turns about
the selection's centre rounded to that grid, as quick turns do in the
NetRadiant Custom and TrenchBroom profiles, so a brush whose sides differ by an
odd number of grid steps stays on the grid:

```sh
vibestudio --cli map rotate room.map --object brush:12 --axis x --turns -1 --pivot-grid 16 --output turned.map --json
vibestudio --cli map flip room.map --object brush:12 --axis z --output flipped.map
```

**--pivot-grid** cannot be combined with **--pivot**. JSON adds the resolved
`pivot` and `pivotGrid` to the save fields. `editor controls <profile>` reports
the profile's `quickTurnPivot` (`grid` or `centre`) and `flipPivot` (`half-grid`
or `centre`), and `editor gestures <profile> --set editing.quickTurnSnapsPivot=on`
or `--set editing.flipSnapsPivot=on` changes them.

**Status: Partial.** The CLI and the Levels commands share one rotation and
mirror service. See [Edit Quake-family maps](manual/levels.md#edit-quake-family-maps).

### Rotate and scale level objects by exact values

`map rotate --euler x,y,z` turns the objects about world X, then Y, then Z by
those degrees, as one step: the command line form of **Rotate Selection…** and
NetRadiant Custom's **Arbitrary Rotation**. It replaces **--axis**,
**--degrees** and **--turns**, and takes the same **--pivot**, **--pivot-grid**,
**--texture-lock** and **--allow-valve220**. `map resize --factors x,y,z`
multiplies the objects' size along each axis about their centre, or about
**--pivot x,y,z**, as **Scale Selection…** and **Arbitrary Scale** do. A
negative factor mirrors along its axis, and faces keep facing outward. Two
numbers leave z at 1, which Doom maps need. **--factors** replaces **--size**
and **--mins**/**--maxs**, and **--pivot** goes only with it:

```sh
vibestudio --cli map rotate room.map --object brush:12 --euler 15,0,45 --pivot 0,0,0 --output turned.map --json
vibestudio --cli map resize room.map --object brush:12 --factors -1,2,1 --texture-lock on --output mirrored.map --json
```

JSON adds `eulerXyz` for a rotation and `factors`, `mirrored`, `pivot` (when
given) and the old and new bounds for a scale. A zero factor, a factor beyond
±32768, a Doom Z factor other than 1 or a Doom turn about X or Y fails with
the reason and writes nothing.

**Status: Partial.** The CLI and the Levels windows share the same services.
See [Rotate and scale by exact values](manual/levels.md#rotate-and-scale-by-exact-values).

### Make rooms with caulk and openings

`map hollow --mode room` is **Make Room**: walls `--thickness` units thick
outside each `--object` brush, which is removed so it becomes the room. Two
options match **Make Room Options…**, and also work with `--mode overlap` and
`--mode touch`:

- **--caulk** puts the game's caulk material on every wall face except the one
  facing the room: `common/caulk` in Quake III (`textures/common/caulk` for
  brushDef3 brushes) and `skip` in Quake. Quake II has none, so there
  **--caulk** fails and you name a material with **--caulk-texture NAME**,
  which also works on its own.
- **--open-face face:brushId:faceNumber**, repeatable, leaves that face open,
  with no wall. Faces are numbered from 1, as `map select-textured` prints
  them, and must belong to a brush `--object` selects. The walls beside an
  opening stop flush with it, and the faces lining it keep its material.

```sh
vibestudio --cli map hollow room.map --object brush:12 --mode room --thickness 16 --caulk --open-face face:12:6 --output room-made.map --json
```

JSON adds `caulkTexture`, `caulkedFaces` (wall faces that now carry it),
`wallCount`, `openFaces` (the selectors given) and `openFaceCount` (faces left
open on the hollowed brushes). A bad material name, a face the brush does not
have or a face on another brush is a usage error (exit 2) and writes nothing; a
brush with every face open is left as it was and listed under `skipped`.

`map hollow` takes its options as flags. To see or change the options the
Levels editor's **Make Room** uses, the ones **Make Room Options…** saves, use
`editor make-room`:

```sh
vibestudio --cli editor make-room --json
vibestudio --cli editor make-room --set caulkTexture=common/nodraw --set openPickedFaces=off --dry-run --json
vibestudio --cli editor make-room --reset
```

Repeat `--set` with `caulk=on|off`, `caulkTexture=NAME` (leave the name out,
as in `caulkTexture=`, for the game's own caulk) and `openPickedFaces=on|off`.
`--reset` brings back NetRadiant Custom's behaviour, and `--dry-run` shows the
result without saving. JSON reports `values`, `applied` and `dryRun`. A field,
value or material name the editor would refuse is a usage error (exit 2).

**Status: Partial.** The CLI and the Levels editor share the same service.
See [Make a room with caulk and openings](manual/levels.md#make-a-room-with-caulk-and-openings).

### Rebuild brushes as prisms, cones and spheres

`map add-shape --in-place` is **Make Prism…**, **Make Cone…**, **Make
Sphere…** and **Make 3-Sided Prism** to **Make 9-Sided Prism**: each
`--object brush:N` is rebuilt in its own bounds as a `box`, `wedge`,
`cylinder`, `cone` or `sphere`, keeping its number, its brush entity, layer or
group, its place in the file and its face format. `--fit circle` builds the
shape as NetRadiant Custom does, round on the larger extent with a face square
to the first axis; `--fit box` (the default) stretches it to the bounds.
`--axis x|y|z` is the depth axis, as the Top (`z`), Front (`y`) and Side (`x`)
views give it.

```sh
vibestudio --cli map add-shape start.map --shape cylinder --sides 6 --axis z --fit circle --in-place --object brush:4 --object brush:7 --texture base/pillar --output pillars.map --json
vibestudio --cli map add-shape start.map --shape sphere --sides 8 --bands 8 --fit circle --in-place --object brush:9 --texture base/orb --output orb.map --json
```

JSON reports `inPlace`, `fit`, the rebuilt `brushes` and `skipped`, the
reasons any brush was left as it was (hidden, or too small for the sides).
Cylinders take up to 126 sides and cones 127; a sphere's sides times bands is
at most 128. `--in-place` with `--mins`/`--maxs`, or with a shape of several
brushes or a patch, is a usage error (exit 2). `map add-brush` takes
`--fit box|circle` too.

**Status: Partial.** The CLI and the Levels editor share the same service.
See [Rebuild brushes as prisms, cones and spheres](manual/levels.md#rebuild-brushes-as-prisms-cones-and-spheres).

### Create a brush from points and list brush corners

`map add-brush --points` adds the convex brush that encloses four or more
points, as **Create Brush from Points…** and TrenchBroom's Create Brush do. The
points are `x,y,z` groups separated by spaces or semicolons, `x y z` points per
line, or TrenchBroom's `(x y z) (x y z)`; repeated `--points` values are joined,
and text after `//` is ignored, so `map brush-components` output and copied
coordinates can be reused. `--texture` is required and goes on every face; the
brush is added to worldspawn in the map's face format.

`--extrude dx,dy,dz` adds a copy of every point moved by that offset before the
brush is built, so a flat outline becomes a prism, as Shift-drag does in the
**Assemble Brush Tool**.

```sh
vibestudio --cli map add-brush start.map --points "0,0,0 128,0,0 0,128,0 0,0,64" --texture base/metal --output start-wedge.map --json
vibestudio --cli map add-brush start.map --points "(256 0 0) (384 0 0) (256 128 0)" --points "(256 0 64) (384 0 64) (256 128 64)" --texture base/metal --output start-ramp.map
vibestudio --cli map add-brush start.map --points "0,0,0 128,0,0 128,64,0 0,64,0" --extrude 0,0,96 --texture base/metal --output start-block.map
```

JSON reports `shape` as `points` and a `points` object with `brushId`,
`inputPoints`, `corners`, `unusedPoints` (inside the brush or on its faces),
`faces` and `faceDialect`. Fewer than four or more than 4,096 points, points in
one place, on one line or on one plane, a malformed point, or `--points` with
`--shape`, `--axis`, `--sides`, `--bands`, `--fit`, `--mins` or `--maxs` is a
usage error (exit 2), as is `--extrude` without `--points` or without three
numbers. With `--extrude`, `inputPoints` counts the moved copies too. See
[Assemble a brush from clicked points](manual/levels.md#assemble-a-brush-from-clicked-points)
for the tool it scripts.

`map brush-components` lists several brushes when `--brush` is repeated or
given IDs separated by commas, in the order given. Each face lists its corners'
coordinates as well as their vertex IDs. Plain output writes coordinates at
full precision, the shortest decimal that reads back exactly, and starts each
brush with `brush N:` when there is more than one. JSON keeps `brush` for the
first brush and adds `brushes` for all of them, each face with a `positions`
array.

```sh
vibestudio --cli map brush-components start.map --brush 0,3 --brush 7
vibestudio --cli map brush-components start.map --brush 0 --brush 3 --json
```

The editing commands (`map move-components` and the others) still take one
brush.

**Status: Partial.** The CLI and the Levels editor share the same services.
See [Create a brush from typed points](manual/levels.md#create-a-brush-from-typed-points)
and [Copy vertex coordinates](manual/levels.md#copy-vertex-coordinates).

### Hollow, shrink and expand as the CSG Tool does

`map hollow` also builds the walls of the **CSG Tool**'s other hollows.
`--mode diagonal` puts them outside each brush, meeting on mitred joints with
no overlap or gap; `--mode extrude` builds a wall straight out from each face,
its sides square to the face. `--mode wrap` is another name for `touch`, and
`--mode pull` for `room`, as NetRadiant Custom names them.

- **--keep-inner** keeps the brush instead of removing it: as it was for the
  outward modes, or what the walls leave of it for `overlap` and `touch`. With
  **--caulk**, the faces between the walls and the brush take caulk and the
  outside keeps its materials.
- **--exclude-face face:brushId:faceNumber**, repeatable, leaves a face alone,
  as **--open-face** does.
- **--only-face face:brushId:faceNumber**, repeatable, works on the named
  faces only; every other face of the brushes `--object` reaches is left open.
- **--exclude-facing x|y|z** or **dx,dy,dz** leaves alone each brush's faces
  at either end along that direction, as the **Faces** choices for the 2D view
  and the camera do.

`map shrink-brushes` and `map expand-brushes` move every face of the
`--object` brushes `--thickness` units in or out, with the same three face
options. Textures stay where they are in the world. A brush a shrink would
flatten, or rob of a face, is left as it was and listed under `skipped`; when
no brush can change, the command fails (exit 1) and writes nothing. `--context`
opens a group first.

```sh
vibestudio --cli map hollow room.map --object brush:12 --mode diagonal --thickness 16 --exclude-facing z --keep-inner --caulk --output shaft.map --json
vibestudio --cli map shrink-brushes room.map --object brush:12 --thickness 8 --exclude-face face:12:6 --output thinner.map --json
vibestudio --cli map expand-brushes room.map --object entity:3 --thickness 4 --output wider.map --json
```

`map hollow` JSON adds `keepInner` and `innerKept`. The shrink and expand JSON
reports `operation`, `thickness`, `offset` (negative for a shrink), `changed`,
`movedFaces`, `excludedFaces` and `skipped`. An unknown mode, a bad direction
or a face selector that does not fit the selection is a usage error (exit 2).

**Status: Partial.** The CLI and the Levels editor's **CSG Tool** share the
same services. See
[Hollow, shrink and expand with the CSG Tool](manual/levels.md#hollow-shrink-and-expand-with-the-csg-tool).

### Query connected level entities

`map select-connected <map> --object patch:2` adds one hop of incoming and
outgoing `target`/`targetname` connections to the supplied object selectors.
Repeat **--object** for more seeds; **--context** limits results to an open group
and repeated **--exclude** supplies view filters. JSON includes `selection`,
`originalSelection`, `added`, `connectionKeys`, `transitive: false`,
`sourceSha256`, `queryOnly: true` and `mapWritten: false`. No map is written.
See [Select connected entities](manual/levels.md#select-connected-entities) for exact
matching, owner expansion, visibility, group and history behaviour.

### Query temporary level visibility

`map visibility <map> --mode inspect|hide|show-all|isolate` runs the shared session
visibility service without writing a map. Hide and Isolate require repeated
**--object** selectors. Repeated **--hidden** and **--shown** selectors supply
explicit session states; either accepts an object selector or **group:UUID**.
A target cannot be both hidden and shown. **--policy inherited** applies TB-style
container hiding and isolation; **--policy flat** targets existing
objects. **--context GROUP** supplies an open group. Show All accepts no
**--object** and resets both hidden and shown overrides.
Use **--map MAP02** to choose a map in a Doom/Hexen WAD; **--engine** supplies
the same optional engine hint as other map queries.

**--profile ID** selects a registered editor profile, defaulting to
**vibestudio-default**. TB defaults to inherited hiding; NRC and VibeStudio default
to flat hiding. Explicit **--policy** overrides that hiding choice. Insertion
behaviour still follows the profile: NRC copies start with fresh inherited
states, while TB/default copies retain explicit descendants and reveal roots.
JSON also reports `profile` and `insertionVisibility`.

JSON returns `hidden` and `shown` (explicit session selectors), `policy`,
`effectiveHidden` (object selectors including saved scene visibility and dependent
geometry), `selection`, `sourceSha256`, `changed`, `queryOnly: true` and
`mapWritten: false`. Feed `effectiveHidden` selectors to selection queries as
**--exclude**, or pass the remaining selection
to authoring commands. No CLI undo session is persisted. **--output**,
**--overwrite**, **--dry-run**, unknown modes and invalid selectors are rejected.
To inspect authoring effects without writing a map, use **--mode move --node
GROUP --parent NODE**, **--mode assign --object SELECTOR --parent NODE**, or
**--mode duplicate --object SELECTOR --offset x,y,z**. Assign and Duplicate
accept repeated object selectors. Omit **--parent** or use **default** for the
Default layer; Duplicate defaults to a zero offset. The same scene and insertion
services apply layer transitions, copied attributes, new-root visibility and
validation. Results also include the resulting `scene`; the source hash and
`mapWritten: false` still refer to the unchanged source. These modes preview an
operation in memory and do not persist an undo session.

**--class light** (repeat it or use commas) adds an entity class filter, as
**Toggle light Visible** does in the studio, with any mode. `classFilters` lists
each class with its `count`, `objects` and `command`, `classFiltered` the
entities the class filters hide, and `viewHidden` everything the views would
hide: `effectiveHidden` and `classFiltered` together. Class filters leave the
session's hidden and shown states as they are.

```text
vibestudio --cli map visibility start.map --mode inspect --class light --json
```

See [Temporary visibility](manual/levels.md#temporary-visibility) for profile behaviour
and remaining native parity limits.

### Query view filters

`map filters <map>` lists the view filters the map's format offers, as the
**Filters** section of the Levels **View** tab does: each kind's `id`, `title`,
`description`, `count`, whether it is `shown`, and the `command` that toggles it
in the studio (`map.filterWorld`). **World Brushes** counts worldspawn,
func_group and func_detail as the world, as Radiant does. Doom maps list their
thing categories (`things:0` and on, and `things:other`).

**--off world,clip** names the kinds to treat as switched off (repeat it or use
commas); a kind the map does not offer is refused. **--saved** starts from the
list the View tab saved in the settings store (choose another store with
**--settings-file**), leaving out kinds saved for other formats. **--invert**
then applies **Invert Filters**. **--objects** adds each kind's object
selectors. `filteredObjects` and `filteredCount` report what the views would
hide; `off` is the final list. Each kind's `faceCount` counts the faces it hides
one at a time on brushes it does not hide whole: **Caulk**'s caulk faces (and
Quake's skip) on otherwise textured brushes, which the camera leaves out.
`filteredFaces` and `filteredFaceCount` list those faces for the final list as
`face:brushId:faceNumber` selectors, numbered from one as `map select-textured`
prints them, and **--objects** adds each kind's `faces`. Use **--map MAP02** for
a map in a Doom or Hexen WAD and **--engine** for the usual engine hint.

**--package baseq3** reads a Quake III map's shader scripts from a package or
folder, as the View tab reads the open package's: **Translucent**, **Liquids**,
**Sky**, **Clip** with **Player Clip** and **Monster Clip**, **Bot Clip** and
**Nodraw** then go by each face's shader (qer_trans, alphaFunc, fog, surfaceparm
water, playerclip, nodraw and the rest, skyParms), and textures the package does
not hold keep the name rules. `shaders` reports the package, how many `scripts`
and `shaders` were read and any `warnings`, and each kind says whether it
`readsShaders`. Without **--package**, and on Quake and Quake II maps, which
carry their own face flags, the kinds go by flags and texture names. **Fog**
reads the shaders too; **Skip**'s `faceCount` counts skip faces as **Caulk**'s
counts caulk faces.

**--tags tags.json** reads filter tags from a tag file or a TrenchBroom game
configuration (its comments, bare names and trailing commas included, with
`"\**"` keeping its backslash), and **--project mymod** from the project manifest's
`levelFilterTags`, as the View tab does. `tags` lists each tag's `id`
(`tag:water`), `name`, `match`, `patterns` or `flags` (with `flagBits` and any
`unresolvedFlags` the map's game does not name), `source`, `description`,
`count`, `faceCount`, `shown` and `command` (`map.filterTag.water`); with
**--objects** also its `objects` and `faces`. `tagSources` and `tagWarnings` say
where the tags came from and which entries were skipped. **--off** takes tag ids
and **--invert** flips tags with the kinds.

**--class light,info_null** switches off entity class filters (repeat it or use
commas). `classes` lists the switched-off classes, or with **--classes** every
class of the map and of **--definitions file** (else the game's built-in
classes) with its `group`, `kind`, `count`, whether the definitions name it
(`defined`), `shown` and `command` (`map.filterClass.light`); `classCount` counts
them. Class ids (`class:light`) also work in **--off**. `filteredObjects` covers
the kinds, tags and classes together, including a brush whose faces would all go
one by one under several of them (caulk and skip sides, say); its faces then
leave `filteredFaces`. A class whose name holds a comma has no filter.

```text
vibestudio --cli map filters start.map --json
vibestudio --cli map filters start.map --off world --invert --objects
vibestudio --cli map filters q3dm1.map --package baseq3 --off translucent --objects
vibestudio --cli map filters q3dm1.map --tags Quake3.cfg --package baseq3 --off tag:fog --json
vibestudio --cli map filters start.map --class light,info_null --classes
```

The query writes nothing: the map, `mapWritten: false`, and the saved filters
stay as they were. See [Look around the map](manual/levels.md#look-around-the-map) for
what each kind names.

### Query a level camera focus

`map camera-focus <map>` reports where **View** > **Camera** would put the
Levels 3D camera, through the same service. **--object brush:12** (repeat it for
more) names what to focus on, as **Focus Camera on Selection** does: the camera
keeps **--camera-yaw** and **--camera-pitch** (degrees, 0 by default; yaw 0
looks along +X, pitch up is positive) and backs off along its view until the
objects' box fits a **--fov** vertical field of view (90 by default) at
**--aspect** width to height (`4:3` by default; `1.6`, `16:9` and `1920x1080`
also work), never nearer than 64 units to the box's centre. **--offset 8**
applies eight free-focus wheel notches, halving the distance; negative values
move farther. **--level** first levels the camera as **Level Camera** does: no
pitch and the heading rounded to the nearest 22.5 degrees. With **--level** and
no objects, the camera stays at **--camera-position x,y,z** (0,0,0 by default).
**--context** opens a group first; hidden objects are refused. Use **--map
MAP02** for a map in a Doom or Hexen WAD and **--engine** for the usual engine
hint.

```text
vibestudio --cli map camera-focus start.map --object brush:12 --camera-yaw 45 --camera-pitch -30 --fov 100 --aspect 16:9 --json
vibestudio --cli map camera-focus start.map --level --camera-yaw 50 --camera-pitch 20 --camera-position 0,0,64
```

JSON reports `camera` (`position`, `yaw`, `pitch`, `fieldOfView`, `aspect`),
`levelled`, and with objects a `focus` object: the `objects`, their box
(`mins`, `maxs`, `centre`), `fitDistance`, `distance` after the offset, `offset`
and `minimumDistance`. The query writes nothing (`mapWritten: false`). See
[Look around the map](manual/levels.md#look-around-the-map) for the commands in the
studio.

### Select level objects by volume

`map select-region` supports **--policy bounds|trenchbroom|radiant**. Queries are
read-only; **--output** writes the profile's prepared result, including source
volume deletion. **--dry-run** validates that output without writing it.
JSON distinguishes new matches from the resulting selection and reports whether
a map was written. See [Select by brush volume](manual/levels.md#select-by-brush-volume)
for consuming semantics, modes, definitions and examples.

#### Apply configured smart tags

**Partial.** `map smart-tag` reads the same tag definitions as the Levels
filters and applies one atomic edit. Give **--tags FILE** or **--project FOLDER**
and **--tag ID** (for example `tag:clip`). **--choices** lists matching material
or brush-class names without changing anything. **--package PATH** supplies
shader definitions for surfaceparm matching; **--definitions FILE** supplies
brush classes.

```sh
vibestudio --cli map smart-tag ./maps/start.map --tags ./tags.json --tag clip --choices --json
vibestudio --cli map smart-tag ./maps/start.map --tags ./tags.json --tag clip --object brush:0 --choice common/clip --output ./maps/clipped.map
vibestudio --cli map smart-tag ./maps/start.map --tags ./tags.json --tag detail --object brush:0 --disable --output ./maps/structural.map
```

Use repeated **--object brush:ID|patch:ID|entity:ID**, or repeated
**--face brushId:faceNumber** (one-based). **--choice NAME** chooses the material
or classname; a single matching name is chosen automatically. **--disable**
clears flag bits or moves matching brush entities to worldspawn. Texture and
surfaceparm tags cannot be disabled. **--context GROUP** opens an editing
context. Every change requires **--output**; **--dry-run** validates without
writing, and **--overwrite** permits the existing safe-save replacement path.

## Troubleshooting and FAQ

This page helps when something goes wrong: VibeStudio will not start, a game or compiler is not found,
a package will not open, or the studio crashed. It also shows where the logs are and what to include
in a bug report. VibeStudio is pre-alpha software, so if nothing here helps, please report the problem.

### VibeStudio will not start

VibeStudio builds are not signed or notarised yet, so your system may warn you before the first start.
Only continue if you got VibeStudio from the project's own
[GitHub repository](https://github.com/themuffinator/VibeStudio) or its
[releases page](https://github.com/themuffinator/VibeStudio/releases).

#### Windows

- **Windows protected your PC** (SmartScreen): choose **More info**, then **Run anyway**.
- A message that `VCRUNTIME140.dll` or `MSVCP140.dll` is missing: install the Microsoft Visual C++
  Redistributable for x64. The portable Windows packages do not include it.
- A message that `Qt6Core.dll` or another Qt file is missing, or a `vibestudio.exe` you built yourself
  exits at once without a message: Windows cannot find Qt. Add Qt's `bin` folder to PATH, as shown in
  [Build from source](manual/building-from-source.md#run-from-the-build-folder). Windows release packages
  include their own Qt files, so this only affects builds you made yourself.

#### macOS

- If macOS says the app cannot be opened because Apple cannot check it for malicious software, or
  because the developer cannot be verified (Gatekeeper), open **System Settings** >
  **Privacy & Security** and choose **Open Anyway**. On macOS 13 and 14 you can also Control-click
  the app and choose **Open**. [Install VibeStudio](manual/install.md) has the details.
- A message about missing Qt libraries from a nightly package: nightly packages for macOS do not
  include the Qt frameworks yet. Use a release, or
  [build VibeStudio from source](manual/building-from-source.md).

#### Linux

- If the AppImage does not start, it usually lacks FUSE 2. Install your distribution's `libfuse2`
  package (`libfuse2t64` on Ubuntu 24.04 and later), or run the AppImage with
  `--appimage-extract-and-run`. Make sure the file is executable first:
  `chmod +x VibeStudio-<version>-linux-x86_64.AppImage`.
- A message that a `GLIBC` version was not found means your system's C library is older than the one
  the build needs. [Install VibeStudio](manual/install.md) lists the systems the AppImage supports; on an
  older system, build from source.
- "error while loading shared libraries: libQt6Widgets.so.6" from a nightly package: nightly packages
  for Linux do not include the Qt libraries yet and need Qt 6.10.1 or newer on the system. Use the
  AppImage, or build VibeStudio from source.

The command line (`vibestudio --cli ...`) needs no display, so it also works over SSH and in CI.
Commands that draw in 3D (`material render`, `render backends`, `render test`) can use only Vulkan
when there is no display; see [3D views stay empty](manual/troubleshooting.md#3d-views-stay-empty).

### 3D views stay empty

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

### Find the logs

Each day's sessions write to a log file named `vibestudio-<date>.log`, with the date as `YYYYMMDD` in
UTC, in a `logs` folder in VibeStudio's per-user data folder. The log records the studio's messages,
warnings and errors.

| System | Log folder |
| --- | --- |
| Windows | `%APPDATA%\DarkMatterProductions\VibeStudio\logs` |
| macOS | `~/Library/Application Support/DarkMatterProductions/VibeStudio/logs` |
| Linux | `~/.local/share/DarkMatterProductions/VibeStudio/logs` |

`vibestudio --cli diagnostics crashes` prints the folder on any system. Crash reports are kept in the
same folder. [Install VibeStudio](manual/install.md) lists where settings and recovery copies are kept.

### Create a diagnostics bundle

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

### Recover after a crash

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

### Start with clean settings

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

### The interface is still in English

VibeStudio is prepared for many languages, but the translations are not finished yet, so the
interface stays in English whichever language you choose. A new language takes effect after a
restart. If you choose a right-to-left language such as Arabic, Hebrew, Persian or Urdu, the layout
mirrors even though the text is still English. To go back, choose English or the
**System language** entry under **Settings** > **Appearance and Language** > **Language**, or run
`vibestudio --cli --set-locale en`. `vibestudio --cli localization report --locale de` shows how far
a language's translation has come.

### Text is too small or too large

- **View** > **Larger Text**, **Smaller Text** and **Reset Text Size** step the text scale between
  100% and 200%.
- **Settings** > **Appearance and Language** > **Text scale** offers 100%, 125%, 150%, 175% and 200%,
  and **Density** makes the layout **Comfortable**, **Standard** or **Compact**.
- VibeStudio also follows your system's display scaling.

From the command line, `vibestudio --cli --set-text-scale 150` sets the scale. See
[Accessibility](manual/accessibility.md) for the other display and reading options.

### Compilers are not found

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
[Build and launch](manual/build-and-launch.md).

### A game is not detected

**Detect Installs** on the Workspace page looks only in common Steam folders, including extra Steam
libraries, and common GOG folders. It recognises a game by its folder name, its program and its data files, such as
`doom2.wad`, `id1/pak0.pak`, `baseq2/pak0.pak` or `baseq3/pak0.pk3`. For a game installed elsewhere,
from another store or as a source port, choose **Add** in **Game Installations** and pick its
folder. To scan another Steam library or game folder, run
`vibestudio --cli install detect --root "<folder>"`. Detected games are not saved until you choose
**Import Detected**. See [Projects and game installations](manual/projects.md#add-your-game-installations).

### A release includes the game's own files

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
  [Package and release](manual/releases.md#requirements-and-expansions).

### A release is missing a file

A release gathers what your maps, models and shader scripts name, and what your QuakeC, Quake III
menus and configs, and DECORATE, ZScript, SNDINFO and MAPINFO name in full. A map release takes only
what the map's own entities and MAPINFO entry use; release the whole project to ship everything.

Files that compiled game code (QVM, DLL, `progs.dat`) or ACS loads, Quake II and Quake III game code
written in C, and files whose names a script builds while the game runs are not found: add them with
`release.include` patterns in the project manifest. The **Problems** list names files that a script
uses but nothing provides. See [Package and release](manual/releases.md#files-that-game-code-and-scripts-load).

### A package will not open

- Check that the format is supported; see [Packages](manual/packages.md#supported-formats).
- Packages with more than 250,000 entries, folders included, or paths more than 128 folders deep are
  refused rather than half loaded. Open a smaller archive or a narrower folder.
- Encrypted ZIP entries, ZIP entries that are neither stored nor DEFLATE-compressed, and compressed
  WAD2 and WAD3 lumps are listed but cannot be read. Split, multi-disk ZIP archives are refused.
- The Activity Center keeps the reason for every failed open. For a damaged archive, run
  `vibestudio --cli package validate <package> --json` to see which entries fail.

### Common questions

#### Game folders stay unchanged

Detection and indexing a game's assets only read, and installation profiles start read-only.
VibeStudio writes into a game folder only for test-map copies you allow and prepared-build deployments
you review, though the game itself may write its own configuration and logs when you launch it.
Releases are written to your project's output folder, never into the game. Removing a profile never
deletes game files.

#### Nothing is sent without your consent

AI features are off by default, and they are the only part of VibeStudio that connects to the
network. Crash reports and diagnostics bundles stay on your computer unless you share them. See
[AI assistant](manual/ai.md).

### Report a bug

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

## Build from source

Build VibeStudio yourself to try the latest changes, debug a problem or contribute. You need a C++20
compiler, Qt 6, Meson, Ninja and Python 3; this page lists them, then walks through configuring,
building, testing, running and packaging.

> [!NOTE]
> The project's CI builds and tests every pull request on Windows, macOS and Linux with Qt 6.10.1.
> Other compilers and Qt versions may work, but CI does not check them.

### What you need

| Requirement | Notes |
| --- | --- |
| A C++20 compiler and a C compiler | Windows: MSVC 2022 or clang-cl. macOS: Xcode's clang. Linux: GCC or Clang. The C compiler builds the bundled audio decoders. |
| Qt 6 | Core, Gui, Widgets and Network. Optional: Multimedia for sound playback, and Test for one GUI test. On Windows, use the `msvc2022_64` kit. |
| Meson and Ninja | For example `python -m pip install meson ninja`. |
| Python 3 | Configuring and building run small Python steps, and the tests and validation scripts are Python. |
| Git | To clone the source with its submodules. |
| Qt Linguist tools, optional | `lrelease` compiles the translation catalogues; without it the build still succeeds. `lupdate` is used by the translation check in the full validation run. |
| ALSA development files, Linux, optional | For the synchronised recording backend, such as `libasound2-dev` on Debian and Ubuntu. Without them, that backend is left out. |
| An OpenGL 3.3 or Vulkan 1.0 driver, for running | The 3D views and many tests draw with it. No Vulkan SDK is needed to build: the Vulkan headers are in the source, and the loader is opened at run time. On Linux without a GPU, Mesa's `mesa-vulkan-drivers` (lavapipe) works; on Windows, `python scripts/install_software_vulkan.py` puts lavapipe and a Vulkan loader in `build/software-vulkan` (it needs 7-Zip) and prints the two settings that use them. |
| CMake 3.28 or newer, optional | Only for `-Dggml=enabled`: `scripts/build_ggml.py` builds llama.cpp with it, and clones llama.cpp from GitHub unless you give `--source`. |
| glslang, optional | Only when you change the shaders in `src/core/shaders`: `python scripts/build_render_shaders.py` regenerates `src/core/render_shader_data.inc` with `glslangValidator` (Vulkan SDK, or `glslang-tools` on Debian and Ubuntu). |

Meson finds Qt through `qmake6`, so put your Qt installation's `bin` folder on PATH, or make Qt
available to `pkg-config`. On Windows, build from a Developer PowerShell or Developer Command Prompt
for Visual Studio 2022 so Meson finds MSVC, or put LLVM's `clang-cl` on PATH. The commands on this
page call `python`; use `python3` where that is its name on your system.

### Get the source

```sh
git clone --recursive https://github.com/themuffinator/VibeStudio.git
cd VibeStudio
```

If you cloned without `--recursive`, fetch the submodules afterwards:

```sh
git submodule update --init --recursive
```

The submodules hold the source of VibeMap2 and VibeMap3, VibeStudio's own compilers (derived from
ericw-tools and from q3map2 in NetRadiant Custom), and of ZDBSP and ZokumBSP, for building the
compilers yourself and for licence review. The VibeStudio build does not compile them, but one of the
tests checks that they are present. VibeMap2 and VibeMap3 build with CMake; when you build them in
place, VibeStudio finds the programs in their build folders.

### Configure, build and test

```sh
meson setup builddir --backend ninja
meson compile -C builddir
meson test -C builddir --print-errorlogs
```

Meson's default build type is `debug`, which runs noticeably slower. CI configures with
`--buildtype=debugoptimized`, which you can add to `meson setup` too. Tests that draw in 3D skip their
drawing checks when neither OpenGL nor Vulkan starts; set `VIBESTUDIO_RENDER_REQUIRE=1` to make that a
failure instead, as CI does on Windows and Linux. Most tests use Qt's offscreen platform, where only Vulkan
can draw on Windows and macOS. On Windows, Qt's `bin` folder
must also be on PATH when the tests run, as in
[Run from the build folder](manual/building-from-source.md#run-from-the-build-folder).

### Use the Windows helper script

`scripts/meson_build.ps1` does the whole loop in one command. It needs PowerShell 7 (`pwsh`). It is
written for Windows, and it also runs on macOS and Linux when `pwsh` is installed.

```powershell
pwsh -NoProfile -File scripts/meson_build.ps1
```

The script finds `qmake6` through the `QMAKE` environment variable, your PATH, the `Qt6_DIR`, `QTDIR`
or `QT_DIR` variables, or `C:\Qt`, and puts Qt's `bin` folder on PATH. On Windows, when `clang-cl` is
on PATH and `CXX` is not set, it builds with `clang-cl`, re-creating a build folder that was set up
with another compiler. It then configures, compiles, runs the tests and runs the repository's
validation scripts.

| Parameter | Effect |
| --- | --- |
| `-BuildDir <folder>` | Build folder to use. The default is `builddir`. |
| `-Backend <name>` | Meson backend. The default is `ninja`. |
| `-WarningLevel <n>` | Meson warning level, such as `2`. |
| `-WarningsAsErrors` | Fails the build on any warning. |
| `-ConfigureOnly` | Configures and stops. |
| `-SkipTests` | Skips the Meson tests. |
| `-SkipValidation` | Skips the validation scripts, which also check the CLI documentation, credits, samples and packaging. |

To check translation extraction without changing your catalogues, put Qt's
`bin` folder on PATH and run `python scripts/extract_translations.py --check --dry-run`.
The check updates temporary source entries for all languages, with translation
text cleared. Reusing existing entries avoids rebuilding every catalogue from
zero. Use `--write` when you want to update the real catalogues and merge their
translations.

### Build options

Set an option when you configure, for example `meson setup builddir -Daudio_playback=disabled`, or
change it later with `meson configure builddir -Daudio_playback=disabled`.

| Option | Default | Effect |
| --- | --- | --- |
| `audio_playback` | `auto` | Plays sounds through Qt Multimedia when it is installed. Without it, the Audio page still decodes, draws and exports sounds. `enabled` makes a missing Qt Multimedia an error. |
| `audio_duplex` | `auto` | The synchronised recording backend: WASAPI, CoreAudio or ALSA. `disabled` builds without it. |
| `compiler_tools` | `disabled` | Reserved for building the external compilers from their submodules; it has no effect yet. |
| `update_channel` | `dev` | `stable`, `beta` or `dev`, recorded in the build and shown in its reports. |
| `github_repo` | `themuffinator/VibeStudio` | The repository, as owner/name, recorded in the build and shown in its reports. |
| `ggml` | `disabled` | Links llama.cpp and ggml for the **Local GGUF Model (ggml)** connector and Quake-MapGen's ggml ranker. Build them first (below). Release packages are built without it. |
| `ggml_prefix` | empty | Where `scripts/build_ggml.py` installed llama.cpp; empty uses `external/ai/ggml`. |

#### Build with ggml

The `ggml` option needs the pinned llama.cpp, built with CMake for the same
compiler and C runtime as VibeStudio. The script builds and installs it into
`external/ai/ggml`, which Git ignores:

```sh
python scripts/build_ggml.py --config Debug
meson setup builddir-ggml -Dggml=enabled
```

Match `--config` to the Meson build type: `Debug` for the default `debug`,
`Release` for `debugoptimized` and `release`. Both install into the same
folder by default, so give each its own with `--prefix`, and pass that folder
to Meson as `-Dggml_prefix`; the script prints the `meson setup` line to use.
Without `--source`, the script clones llama.cpp at the pinned revision; give
`--source <folder>` to build an existing clean checkout of that revision.

On Windows, build llama.cpp with the same compiler as VibeStudio: set `CC` and
`CXX` to `clang-cl` if that is what `scripts/meson_build.ps1` picked, or run the
script from the same Developer PowerShell for MSVC.

To test the build with a small chat model, set `VIBESTUDIO_TEST_GGUF` to its
`.gguf` path and run `meson test -C builddir-ggml ai-gguf-smoke`; without it,
that test is skipped. The released packages are built without ggml. Builds
without it lose nothing else: **Local/Offline Runtime** can still reach
llama.cpp's own server.

### Run from the build folder

On Linux and macOS:

```sh
./builddir/src/vibestudio
./builddir/src/vibestudio --cli --help
```

On Windows, put Qt's `bin` folder on PATH first, or Windows cannot find the Qt libraries and the
program exits at once:

```powershell
$env:PATH = "C:\Qt\6.10.1\msvc2022_64\bin;$env:PATH"
.\builddir\src\vibestudio.exe
.\builddir\src\vibestudio.exe --cli --help
```

Adjust the path to your Qt version and kit. To keep test runs away from your own settings, add
`--settings-file` with a scratch file, as described in
[Troubleshooting and FAQ](manual/troubleshooting.md#start-with-clean-settings). `--self-test` builds and
paints every page, then exits; CI runs it with the environment variable `QT_QPA_PLATFORM=offscreen`.

### Work in Visual Studio Code

The repository's `.vscode` folder holds tasks and debug configurations. The tasks run the helper
script through `pwsh`, so install PowerShell 7 on macOS and Linux too. Run them from
**Terminal** > **Run Task**.

| Task | What it does |
| --- | --- |
| **VibeStudio: build and validate** | The default build task: configures, compiles with warnings as errors, runs the tests and runs the validation scripts. |
| **VibeStudio: compile** | Configures if needed, then compiles only. |
| **VibeStudio: test** | The default test task: compiles, then runs the Meson tests without the validation scripts. |
| **VibeStudio: build and launch app** | Compiles, then starts the studio with Qt on PATH. |
| **VibeStudio: configure** | Configures only. |
| **VibeStudio: strict warnings build** | Compiles with warnings as errors, without tests. |
| **VibeStudio: launch app (no build)** | Starts the program you already built. **VibeStudio: launch CLI help (no build)** prints its command-line help. |
| **VibeStudio: build docs site** | Renders this manual as HTML in `build/docs-site` and reports any broken link. |
| **VibeStudio: regenerate branding** | Rebuilds the logo, icons and installer art in `assets/branding`. |
| **VibeStudio: check changelog and version** | Validates `CHANGELOG.md`. |

The **Run and Debug** view offers **VibeStudio: debug app** and **VibeStudio: debug CLI help**, each
in four versions: Windows or Unix, and build first or no build. The Windows versions use the Visual
Studio debugger and put `C:\Qt\6.10.1\msvc2022_64\bin` on PATH; edit `.vscode/launch.json` if your
Qt is elsewhere. The Unix versions use GDB on Linux and LLDB on macOS. Debugging needs Microsoft's
C/C++ extension.

### Make a portable package

```sh
python scripts/package_portable.py --binary builddir/src/vibestudio --archive
```

On Windows, pass `builddir/src/vibestudio.exe`. The script stages a versioned folder in `dist`, or in
the folder given with `--output`, with the program under `bin`, the documentation including the
offline user guide, the translation catalogues, the sample projects (leave them out with
`--no-samples`), the licences, platform notes, `package-manifest.json` and `CHECKSUMS.sha256`.
`--archive` also writes a ZIP file, and `--docs-site build/docs-site` adds the HTML manual (build it
first with `python scripts/build_docs_site.py`, which needs `pip install -r scripts/requirements-docs.txt`).
The package does not include the Qt libraries: the release scripts add them for each platform, as
[Releasing](RELEASING.md#run-the-packaging-steps-locally) explains.

### Learn more

- [Contributing](CONTRIBUTING.md): task sizing, validation expectations and credits rules.
- [Architecture](ARCHITECTURE.md): how the studio's modules fit together.
- [Dependencies](DEPENDENCIES.md): every library and tool the build uses.
- [Packaging](PACKAGING.md): release packaging and validation.
