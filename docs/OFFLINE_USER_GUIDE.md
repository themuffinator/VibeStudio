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
| [Level editing: Quake family](manual/levels.md) | Edit Quake and Quake II `.map` brushes, entities and texture alignment in a four-view workspace; prefabs; compile with ericw-tools | Partial |
| [Level editing: Quake III](manual/levels.md) | Brushes, curved patches and shader images in the 3D view; compile with q3map2 and package the result as a PK3 | Partial |
| [Familiar editor controls](manual/editor-profiles.md) | 19 editor profiles bring the keys, mouse gestures, camera and layout of editors such as TrenchBroom, NetRadiant Custom, GtkRadiant, Hammer and Ultimate Doom Builder | Partial |
| [Models](manual/models.md) | View and animate MDL, MD2 and MD3 models; edit meshes, including imported OBJ files, and export MDL, MD2 or MD3; design simple props; assemble tagged models; build Quake III player packages | Partial |
| [Textures](manual/textures.md) | Browse images, sprites and palettes; paint in the layered Texture Editor; export PNG, TGA, PCX, Quake WAD2, Quake II WAL and Doom flats and patches | Partial |
| [Audio](manual/audio.md) | Preview sounds in packages; edit WAV, MP3, FLAC, Ogg Vorbis and Doom sounds; multitrack sessions; deliver sounds in each game's format. Linux builds cannot play or record audio yet | Partial |
| [Packages](manual/packages.md) | Browse, extract, validate and compare PAK, WAD, ZIP and PK3 files and plain folders; stage changes with undo; save a new package; drafts and automatic recovery | Available |
| [Code and scripts](manual/code.md) | Edit QuakeC, shader scripts, configs and other text with highlighting, project-wide search and replace, and optional local language servers | Partial |
| [Materials and shaders](manual/materials.md) | Every texture, Quake III shader and Doom 3 material of idTech 1 to 4: a live, animated preview drawn by each engine's rules, editing as text or as nodes, checks the game would make, and saving into the package | Partial |
| [Build and launch](manual/build-and-launch.md) | Run chained compile pipelines with ericw-tools, q3map2, ZDBSP or ZokumBSP, jump to problems and leaks, and launch the game with your map. You provide the compiler programs | Partial |
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
| Everywhere | Views are drawn in software, without GPU acceleration. Performance on large maps and packages has not been measured. |
| Level editing | The 3D view shows textures, but not Quake III shader effects such as blending, animation and deformation, nor Doom sector lighting and skies. Doom maps need an external node builder after geometry edits. |
| Editor profiles | Profiles reproduce keys, mouse gestures, camera and layout, not every behaviour of the original editors, and they do not add other engines' formats. |
| Models | Formats from Doom source ports to the Doom 3 family are read, but only tested against files the tests build, not real game files. Skeletal models are posed into frames; joints and weights cannot be edited yet. MD5, IQM and ASE exports have not been loaded in the original games. |
| Textures | No sprite animation editing or pressure-sensitive painting. A canvas holds at most 4,194,304 pixels (2048 × 2048). |
| Audio | Linux builds cannot play or record audio. |
| Packages | Renaming or deleting files does not update references to them in scripts, shaders or metadata. |
| Code | Language-server support is an early client that some servers will not work with. |
| Materials and shaders | The preview is a software approximation on a single shape, without map lightmaps or Doom 3 fragment programs beyond the light interaction; videos show a placeholder. Return to Castle Wolfenstein, Enemy Territory and Jedi Knight shader keywords are read with a warning, but only Enemy Territory's implicit images are drawn. Not yet tried on real game packages. |
| Build and launch | VibeStudio does not include the compiler programs; you point it at your own. |
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
     a summary to the clipboard: version, platform, the open project and
     package, the compilers found, the number of crash reports, and the latest
     lines of the session log. Paste it into the issue. It contains folder
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
| Workspace and Editor Profile | The editor profile the Levels page follows | **Choose Editor Profile** |
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
each compiler tool (ericw-tools for Quake and Quake II, q3map2 for Quake III,
ZDBSP and ZokumBSP for Doom), whether it was found, and where its path comes
from. VibeStudio also looks on your `PATH`.

1. Select a tool whose status is **Not found**.
2. Choose **Locate…** and pick its program.
3. After you install a tool somewhere else, choose **Rescan**.

The compiler item in the status bar shows how many tools were found and opens
this tab. See [Build and launch](manual/build-and-launch.md).

### AI and automation

**Open AI Settings** opens **Settings** > **AI and Automation**. **AI-free mode**
is on by default: no AI feature runs and nothing is sent anywhere. To use the
Assistant or AI-assisted generation, clear **AI-free mode**, choose a connector
and model, and select **Allow cloud AI connectors** if the model is not on your
computer. See [AI assistant](manual/ai.md).

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
vibestudio --cli install detect --json
vibestudio --cli project init ./mymod
vibestudio --cli compiler set-path ericw-qbsp --executable /opt/ericw-tools/bin/qbsp
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
| Run any command | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd> or <kbd>Ctrl</kbd>+<kbd>K</kbd> | Type part of its name, category or description, and press <kbd>Enter</kbd> |
| Open a file | <kbd>Ctrl</kbd>+<kbd>P</kbd> (**File** > **Go to File…**) | Type part of a file or folder name, and press <kbd>Enter</kbd> to open it on the right page |
| Search the current page | <kbd>Ctrl</kbd>+<kbd>F</kbd> (**Edit** > **Find**) | Type in the page's filter or search field; on the **Code** page, the editor's find bar opens |
| Search the whole project | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd> (**Edit** > **Find In Project Files…**) | Search, and optionally replace, in every text file of the open project |

The Command Palette and Go to File match loosely, so you can skip letters:
`opk` finds **Open Package**, and `bwall` finds `brick_wall.tga`. Commands that
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
project's folders, its game installation, compiler paths and per-project settings. The Workspace and
Build pages and the command line read it.

To create or refresh it, open the project and choose **Initialize Manifest** on the Workspace header,
or **File** > **Initialize Project Manifest** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd>). A new
manifest treats the whole project folder as source, uses `build` as the output folder and keeps
temporary files in `.vibestudio/tmp`. Refreshing an existing manifest keeps its folders and records
the game installation and editor profile you are using now.

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
| `schemaVersion` | Manifest format version, currently 1. |
| `projectId` | Stable identifier. Made from the folder name when missing. |
| `displayName` | The name the studio shows. |
| `sourceFolders` | Folders that hold your sources. |
| `packageFolders` | Asset folders. `map textures --project-root` checks a map's textures against them. |
| `outputFolder` | Where the Build page writes its command manifests. |
| `tempFolder` | Folder for temporary files. |
| `selectedInstallationId` | The game installation profile this project uses. |
| `compilerSearchPaths` | Extra folders to search for compiler programs. |
| `compilerToolOverrides` | Exact compiler programs, as a list of `toolId` and `executablePath` pairs. These win over paths chosen on the Build page while the project is open. |
| `registeredOutputPaths` | Compiled outputs recorded for the project. |
| `settingsOverrides` | Per-project choices: `selectedInstallationId`, `editorProfileId`, `paletteId`, `compilerProfileId` and `aiFreeMode`. Setting `aiFreeMode` to `true` keeps AI off while the project is open; `false` cannot turn AI on. |
| `createdUtc`, `updatedUtc` | When the manifest was created and last saved. |

</details>

**File** > **Copy Project Manifest** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>M</kbd>) copies the resolved
manifest, for example to attach to a bug report.

### Check project health

Choose **Project** > **Validate Project** to run the health checks. VibeStudio switches to the
Workspace page and lists anything that needs attention under **Problems**: a missing manifest or
folder, a newer manifest version, or no linked game installation. Warnings alone do not block your
work. `vibestudio --cli project validate` exits with code 4 only for blocking problems, and with code 3
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
`id1/pak0.pak` for Quake. Detection only reads; nothing is saved until you import a candidate.

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

- [Game installations](GAME_INSTALLATIONS.md): profile data, detection sources and safety rules.
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
| Header | **Dependencies**, **Save**, **New Map**, **Generate**, **Edit with AI**, **Open Map** |
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
  **Transform**, **Align**, **Select**, **Entities**, **Surfaces**, **Patches**, **Sectors and Lines** and **Region and
  Build**. Each button is the command itself, so it greys out, stays pressed and takes keys just as in the menus; a
  group the map's format cannot use is hidden.
- Right-click a tab to move it to the other sidebar, fold the sidebar, show captions under the tab icons, or
  **Reset Sidebars** to where your editor profile keeps them.
- The **View** menu has **Level Sidebars** commands to show each tab and to fold either sidebar. Like every
  command, they are in command search and can be given keys in **Help** > **Keyboard Shortcuts**.
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
- **Grid.** Choose **Grid 1** to **Grid 256** in the grid list, or press <kbd>[</kbd> and <kbd>]</kbd>. With **Snap**
  ticked, drags and arrow-key nudges move in whole grid steps.
- **3D view.** Choose **3D** to show the camera. The VibeStudio Default profile orbits an orthographic view of the map;
  most other profiles fly a first-person camera. Tick **Textures** above the views to draw package images: open the
  game's package or asset folder on the **Packages** page first. **View** > **3D Wireframe** (<kbd>W</kbd>) shows edges
  only.
- **Layout.** The **Layout** menu shows one 2D view, one 3D view, the camera beside a 2D view, four views,
  **Camera Above Plans** (a wide camera above top, front and side), or **Camera Beside Plans**
  (a tall camera beside three stacked plans). Drag the dividers to size the panes;
  each layout remembers its own proportions. Grid, snap and selection framing remain available in a maximised camera.
  **Follow Editor Profile** returns to your profile's arrangement. **Maximize Active View** enlarges the focused pane
  until you restore it, **Equalize View Sizes** evens out the panes, and the link options keep the 2D views centred
  together or following the camera. **Saved Views** stores named camera and plan positions for this map. The
  **Layout** section of the **View** tab offers the same choices as tiles, each picturing its panes with the camera
  pane filled; point at a tile for its full name.
- **Show.** The **Show** menu chooses what the 2D views draw: **Things** (Doom) or **Entities** (Quake), **Sector Fill**
  (Doom), **Grid**, **Vertices**, **Labels** and **Target Links** (Quake). The **View** tab has the same options with
  the grid, snapping, texture locks, layouts and view links.
- **Filters.** The **Filters** section of the **View** tab hides whole kinds of object in every view, each with a
  count: world brushes, brush entities, detail, clip, hint and skip, caulk, sky, liquids, point entities, lights,
  triggers, monsters, items, player starts, paths and models, or on a Doom map each category of thing. **Show Every
  Kind** brings them all back. Filters are separate from hidden objects, so **Show All Hidden** leaves them on, and
  they only change what you see.
- **Region.** To work on one area of a big map, choose **Set to Selection** or **Set to View** in the **Region**
  section of the **View** tab (or **Select** > **Set Region to Selection**). The views shade or hide what lies
  outside the box, as Radiant's regions and Hammer's cordon do; **Clear Region** brings the whole map back.
  **Compile Region** writes the region beside the map as `<name>-region.map`, unsaved edits included, and compiles
  it with the chosen profile, so `<name>-region` can be loaded in the game to test that area quickly. The region
  map keeps what touches the box, sealed by six brushes just outside it, and gets a player start (where the camera
  stands, or in the middle) if it has none. **Save Region As…** writes the same map anywhere you choose. **Run
  Profile** still builds the whole map.
- **Hide and show.** <kbd>H</kbd> hides the selection, <kbd>Shift</kbd>+<kbd>H</kbd> shows everything again, and
  **View** > **Isolate Selection** hides everything else. Hidden objects stay in the map, its builds and its packages.
  To keep visibility with the map, use the check boxes on the **Scene** tab, which can also lock layers and groups.
- **Export Image** writes an SVG picture of the map for reviews and documentation.

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
- The **Edit** menu adds **Select All of This Class**, **Select by Texture…**, **Select Targets**, **Select Sources**
  and, for Doom maps, **Select Connected Geometry**.
- **Select by Region**, in the right-click menu and the **Select** menu above the views, uses the selection as a
  region, as the Radiant editors do: **Select Inside** keeps what lies wholly inside it, **Select Touching** what
  touches it, and **Select Complete Tall** and **Select Partial Tall** do the same seen from the active 2D view,
  whatever the depth. Hidden and filtered objects are left out.
- The **Transform** section at the top of the **Inspector** tab shows where the selection's centre is and how big it
  is. Type a new centre to move it there, or a new size to resize it, keeping its lower corner; point entities
  move but have no size to change.
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
| Place models and sounds | The **Models** and **Sounds** tabs list the open package's models and sounds, with a preview of the chosen model and a waveform you can play. Drag one onto a view, or choose **Place in View**, for a `misc_model` or a speaker entity; **Give to Selection** sets the selected entities' `model` or `noise` key instead. The **Place** section names the class it makes in the open map, and says so when your definitions do not declare it: Quake itself has no model or speaker entity, so a Quake mod must provide them. |
| Duplicate or delete | <kbd>Ctrl</kbd>+<kbd>D</kbd> copies the selection one grid step over; **Duplicate with Offset…** previews an exact offset and a **Copies** count for a linear array. <kbd>Delete</kbd> removes the selection; an entity takes its brushes with it. |
| Move | Drag the selection, nudge it with the arrow keys (<kbd>Shift</kbd> moves eight grid steps; Radiant-style profiles nudge with <kbd>Alt</kbd>+arrow keys), or use **Move Selection…**. |
| Rotate or flip | **Rotate 90° Left**, **Rotate 90° Right**, **Flip Horizontal** and **Flip Vertical** work as the active view shows the map. **Rotate Selection…** previews any angle about the selection centre, the world origin or your own pivot. |
| Align | **Transform** > **Align Left**, **Align Right**, **Align Top**, **Align Bottom**, **Centre Horizontally** and **Centre Vertically** line the selected objects up on the edge or centre of the whole selection, as the active 2D view shows it, as Hammer does. An entity moves with its brushes. |
| Shear | Turn on **Transform** > **Shear Tool** and drag the handle in the middle of a side of the selection in any 2D view: that side slides along itself and the opposite side stays put, slanting the selection, as TrenchBroom's shear tool does. <kbd>Escape</kbd> turns the tool off. **Transform** > **Shear…** slants the selection about its centre by an angle you type. Texture lock follows **Texture Lock**; where the map's face format cannot slant textures, the tool shears without it and says so. |
| Resize | Drag the handles in a 2D view, or the labelled X, Y and Z handles in the 3D view. **Resize Selection…** takes exact sizes and the point that stays in place. |
| Cut with a plane | Turn on the **Clip Tool** (<kbd>X</kbd>), drag a line across the brushes in a 2D view, press <kbd>Tab</kbd> to choose which side stays (or both), then <kbd>Enter</kbd>. **Clip Selection…** cuts along an exact x, y or z plane. |
| Hollow | **Hollow…** turns each selected brush into walls of the thickness you choose, one per face. |
| Carve (CSG subtract) | **Carve** cuts the selected brushes out of every brush they overlap. The carving brushes stay selected, ready to delete; hidden brushes are left alone. |
| Intersect (CSG) | **Intersect** replaces the selected brushes with the one brush where they all overlap, as TrenchBroom does; each face keeps the texture of the face it came from. |
| Make detail | **Make Detail** stops brushes sealing the map or splitting its visibility: in Quake II and Quake III maps it sets each face's detail flag, and in Quake maps it moves the brushes into a `func_detail` for ericw-tools. **Make Structural** undoes it. |
| Drop to the floor | **Drop to Floor** moves the selected point entities straight down onto the brush or patch below, keeping each class's height above it. |
| Merge brushes | **Merge Brushes…** previews their convex union and lets you choose where conflicting materials, mappings and flags come from. |
| Apply a texture | Select brushes or patches and choose **Apply Texture…**, or press <kbd>Enter</kbd> on a tile on the **Textures** tab. **Replace Texture…** swaps one texture for another across the map or the selection. |
| Align textures | Edit a classic or Valve 220 face's shift, rotation and scale on the **Inspector** tab, or use the **Surfaces** tab: **Target** chooses the selection or the inspected face, **Adjust** shifts, rotates, scales, fits or centres it in steps, and **Copy and Paste** carries one face's material, mapping and flags to others. **Surface Alignment…** gives a detailed preview. |
| Paint materials | Choose a material in the **Material** box above the views, set the camera tool to **Paint**, then click or drag across surfaces in the 3D view. Each stroke is one undo step. **Sample** picks up a surface's material instead. |
| Reshape a brush | **Edit Brush Components…** moves its vertices, edges or faces and rebuilds the brush. |
| Make curves (Quake III) | **Add Patch…** creates a plane, cylinder or cone. **Edit Patch Control Points…**, **Stitch Patches…** and **Cap Patch…** reshape, join and close patches. |
| Link entities | Select the entities, picking the target last, and choose **Connect Entities**. The others target it, and it gets a `targetname` if it has none. |
| Replace key values | **Edit** > **Replace Key Values…**, or the search button on the **Inspector**'s **Properties** section, finds a value of one key across the map's entities (or the selected ones) and replaces it: the whole value, or with **Match the whole value** off, every occurrence, such as a name's prefix. It counts the matches as you type, and the change is one undo step. |
| Reuse a group of objects | **Export Selection as Prefab…** or **Save Selection…** on the **Prefabs** tab saves a `.vprefab` file. **Insert Prefab…** places one; the **Prefabs** tab lists those in the project and the open package. |

For repeated steps, columns or props, set **Copies** in **Duplicate with Offset…** to 1–256 additional copies.
With an X offset of 64, the first copy moves 64 units from the source, the second 128, and the third 192.
The preview shows every copy; **Place** selects them all in one undo step. Copies preserve their owners, textures and
scene memberships. The same workflow copies Doom/Hexen/UDMF things. UDMF copies retain fractional XYZ positions,
comments and unknown thing properties; geometry duplication is unavailable. Entity target
names retain ordinary duplicate behaviour: use **Insert Prefab…** when each linked assembly needs independent target
names. An invalid copy or cancellation leaves the entire map unchanged.

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

1. Choose a shape. Its settings appear below it; the box has none.
2. Choose **Draw in a View**, then drag a box in a 2D view or the 3D view. **Add at View Centre** puts one in the
   middle of the active 2D view instead.
3. To turn existing brushes into the shape, select them and choose **Replace Selected Brushes**: the shape fills their
   bounds, as the Radiant editors' arbitrary-sided brush commands do. Brushes of a brush entity are left alone.

| Setting | What it does |
| --- | --- |
| **Axis** | The axis a cylinder or cone runs along, a sphere's poles, a wedge's height, and what an arch or ring turns about. **Across the View** follows the view you draw in, so an arch drawn in the **Front** view stands up as a doorway. |
| **Sides** or **Segments** | How round a cylinder, cone or sphere is, or how many brushes make an arch or ring. |
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

#### Repeat parts with linked copies

Linked copies keep repeated parts of a level the same, as TrenchBroom's linked groups do: a row of pillars, a
window frame, a lamp with its light. An edit inside one copy is made to every copy, each in its own place.

1. In the **Scene** section of the **Outliner** tab, type a name and choose **New group**, then select the objects
   and choose **Assign selection**.
2. Select something in the group and choose **Create Linked Copy** (**Tools** tab, **Linked Groups**). The copy is
   a new group beside the first, named after it ("Pillar 2"), with **Linked** after its name in the **Scene** tree.
   Move it where you want it: moving a whole copy moves only that copy.
3. Edit either copy: change a brush, a key, a texture's alignment, add or delete objects in the group. The other
   copies take on the change at once, and one **Undo** puts every copy back.

Each copy keeps its own turn: rotate a whole copy by 90 degrees about the vertical, or flip it, and only that copy
turns, while later edits reach it turned the same way. Turns by other angles turn every copy. A locked copy is left
as it is until it is unlocked. If one edit changes copies in different ways, they are left as they are;
select the copy the others should match and choose **Update Linked Copies**. **Separate Linked Copy** makes a copy
independent again, and **Select Linked Copies** selects every copy. Linked copies work on Quake-family maps.

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

### Check maps and entities

- **Health tab.** Lists map problems, entity problems, leaks and compiler checks. With a package open, it also checks
  every texture the map uses. Press <kbd>Enter</kbd> on an entity problem to select that entity.
- **Map tab.** **Worldspawn** edits the map's own keys; **Add Key…** suggests the ones its definition knows.
  **Checklist** shows what a build needs and what is missing, such as a player start, lights, a title, a `wad` key
  for Quake or deathmatch starts for Doom; press <kbd>Enter</kbd> on a row to go to what it is about.
- **Entity definitions.** On the **Map** tab, give a Radiant `.def`, Valve `.fgd` or Quake III `.ent` file, or a
  folder of them, under **Entity Definitions** and choose **Load** (**Browse** picks a file). Left empty, VibeStudio
  looks in the project's `.vibestudio/definitions`, `definitions`, `defs`, `scripts`, `base/scripts` and `entities`
  folders. **Project** > **Load Entity Definitions…** does the same. The map is then checked for unknown classes,
  undeclared keys, invalid values, missing required keys, unknown spawnflags, and targets that name nothing. An `.ent`
  file lists placed entities only, so it cannot check key types or spawnflag names.
- **Built-in classes.** With no definitions found, a Quake-family map is checked against the classes of the stock
  game it looks like: Quake, Quake II or Quake III Arena, taken from id Software's GPL game code, with Quake's
  ericw-tools compiler classes such as `func_detail`. A mod's own classes read as unknown until you load its
  definitions, and **Health** and the **Checklist** say which classes were used.
- **Dependencies.** Open the map's package or asset folder on the **Packages** page, then choose **Dependencies** in
  the header. The scan lists textures, shader images, models, model materials and sounds, marking missing and
  ambiguous ones. Tick **Problems only** to filter, **Select in Map** to find the objects that use an asset,
  **Copy JSON** for the full report, and **Export Assets…** to write the resolved files to a new package.
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
model. You review the layout, then open it in Levels as a new, unsaved map or save it. **Edit with AI** asks your text
model for changes to an open Quake-family map and lists each proposed edit for review before anything changes. AI is
off by default; see [AI assistant](manual/ai.md).

### Work from the keyboard

Every command is also in the menus and in command search. Your [editor profile](manual/editor-profiles.md) can change these
keys, and your own keys in **Help** > **Keyboard Shortcuts** take priority over both.

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
| Nudge the selection (2D view) | Arrow keys, one grid step; <kbd>Shift</kbd>+arrow keys, eight steps |
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
| `map find` | List the objects a query matches, as the **Objects** filter does |
| `map new` | Create an empty map or starter room |
| `map edit` | Change entity keys, a face's texture and alignment, or Doom sector, linedef and side fields |
| `map add-entity`, `map add-brush`, `map add-thing` | Add a point entity, a brush shape or a Doom thing |
| `map delete`, `map duplicate`, `map paste` | Delete objects, copy them with an offset (`map duplicate --copies N` creates a linear array), or insert `.map` text |
| `map move`, `map rotate`, `map flip`, `map resize`, `map snap` | Transform objects, with texture lock options |
| `map add-shape` | Add a shape of the **Shapes** tab, or replace brushes with one |
| `map clip`, `map hollow`, `map carve`, `map intersect`, `map merge-brushes` | Brush tools |
| `map tie-entity`, `map move-to-world`, `map detail` | Make brush entities, give their brushes back to the world, and make brushes detail |
| `map select-region`, `map drop-to-floor` | List the objects a region selection picks, and drop entities to the floor |
| `map align`, `map shear`, `map region` | Line objects up, slant them, and write a region as a sealed map of its own |
| `map replace-key` | Find and replace a key's values across the entities |
| `map apply-texture`, `map replace-texture`, `map align-textures` | Texture tools |
| `map add-patch`, `map edit-patch`, `map stitch-patches`, `map cap-patch` | Quake III patch tools |
| `map connect` | Make entities target the last one given |
| `map draw-sector`, `map draw-stairs`, `map draw-grid`, `map split-linedef`, `map curve-linedefs`, `map flip-linedef`, `map merge-vertices` | Doom geometry tools |
| `map join-sectors`, `map merge-sectors`, `map make-door` | Doom sector tools |
| `map align-walls` | Line up Doom wall textures along the walls joined to one side |
| `map make-sector` | Make a Doom sector of the lines around a point |
| `map shift-sectors`, `map gradient-sectors` | Raise, lower or grade Doom sectors |
| `map inspect-udmf`, `map edit-udmf` | Read and edit UDMF properties |
| `map textures`, `map dependencies` | Check a map's textures and assets against a package or folder |
| `map render` | Draw an SVG picture of a map |
| `map compile-plan` | Plan a compile with a compiler profile |
| `map recoveries`, `map recover` | List recovery checkpoints and save one to a file |
| `map generate` | Generate a level from a description |
| `entity definitions`, `entity validate` | List entity classes, and check a map's entities against them |
| `editor scene link`, `editor scene update-links`, `editor scene unlink` | Make a linked copy of a scene group, make its copies match it, or separate it |

`vibestudio --cli cli commands` lists every command; see [Command line](manual/cli.md).

</details>

### Learn more

- [Editor profiles and controls](manual/editor-profiles.md)
- [Level editor design and acceptance notes](LEVEL_EDITOR.md)
- [Scene layers, groups and locks](LEVEL_SCENE.md)
- [Placed model appearances](LEVEL_MODEL_APPEARANCE.md)
- [Map format support](SUPPORT_MATRIX.md)

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
- **Layout and grid:** how the Levels views are arranged, and the grid size you start with when you switch profile.
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
2. Change fields on the **2D Plan**, **3D Camera** and **Camera Keys** tabs. **Profile default** follows the built-in
   value. Key fields also offer **Custom key**, where you press the key, and **None**, which turns the key off.
3. Read the status line under the tabs. A conflict, such as two gestures on one button, disables **Apply**. If a
   camera key might take priority over a command's key, a button such as **2 possible shortcut overlaps** lists them.
4. Choose **Apply**. Closing the window without applying drops your changes, and **Restore Defaults** sets every field
   back to the profile's own values, ready to apply.

Movement keys are single keys without <kbd>Ctrl</kbd>, <kbd>Alt</kbd> or <kbd>Shift</kbd>, and one key cannot move two
ways. <kbd>Esc</kbd>, <kbd>Tab</kbd>, modifier keys and lock keys are reserved. **Toggle mouse look** may use modifiers.
**Hold to pan** (2D Plan) and **Hold for mouse look** (Camera Keys) work while you hold the key, and stop when you
release it or the view loses focus.

**Export…** saves your changes to a VibeStudio `.json` file, and **Import…** loads one into the window for you to
apply. A file only imports into the profile it was made for.

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

### Use the command line

The `editor` commands read and change the same settings as the studio. Profile IDs and aliases such as `hammer++`,
`udb` or `slade3` both work; settings always store the profile's ID. A change made on the command line applies the
next time the studio loads its settings; it does not control a studio that is already running.

```sh
vibestudio --cli editor profiles
vibestudio --cli editor controls trenchbroom
vibestudio --cli editor select netradiant-custom
vibestudio --cli editor gestures hammer --set camera.flyKeys.forward=I --dry-run --json
```

| Command | What it does |
| --- | --- |
| `editor profiles` | List every profile with its bindings |
| `editor current` | Show the selected profile |
| `editor select <profile>` | Select a profile |
| `editor controls [profile]` | Print a profile's layout, grid, 2D gestures, 3D camera and keys, with your changes |
| `editor gestures [profile]` | Show, change (`--set field=value`), reset (`--reset`), import (`--input`) or export (`--output`) gesture settings |
| `editor layout [preference]` | Show or set the layout: `profile`, `single-2d`, `single-3d`, `camera-and-plan`, `four-views`, `camera-above-plans` or `camera-beside-plans` |
| `editor view-links` | Show or set the linked 2D view and camera-follow options |
| `editor keys` | List the keys you gave commands; `--reset` puts every default back |

Add `--json` for machine-readable output, and `--settings-file <path>` to work on a separate settings file instead of
your own. [Command line](manual/cli.md) covers the rest.

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

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Right drag, middle drag | Middle drag |
| Zoom | Wheel | <kbd>Shift</kbd>+wheel zooms the field of view |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click picks a face |
| Select by area | <kbd>Shift</kbd>+left drag | — |
| Draw a brush | Left drag over empty space (<kbd>Shift</kbd> square, <kbd>Ctrl</kbd>+<kbd>Shift</kbd> cube) | — |
| Move the selection | Arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
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
| Smaller Grid / Larger Grid | <kbd>-</kbd>, <kbd>[</kbd> / <kbd>+</kbd>, <kbd>=</kbd>, <kbd>]</kbd> |
| Snap to Grid | <kbd>Alt</kbd>+<kbd>0</kbd> |
| Cycle Map View | <kbd>Space</kbd> |
| Frame Selection | <kbd>Ctrl</kbd>+<kbd>U</kbd> |
| Carve | <kbd>Ctrl</kbd>+<kbd>K</kbd> |
| Hollow | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>K</kbd> |
| Show Grid | <kbd>0</kbd> |
| Grid 1 to 256 | <kbd>1</kbd> to <kbd>9</kbd> |

**3D Wireframe** and **Draw Sector** have no key. Not implemented yet: Face Mode (<kbd>F</kbd>) and Vertex Mode (<kbd>V</kbd>). TrenchBroom has no rubber band; this profile adds one on <kbd>Shift</kbd>+drag. <kbd>Ctrl</kbd>+<kbd>F</kbd> stays **Find** instead of flipping objects.

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
| Move the selection | <kbd>Alt</kbd>+arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles, or over empty space beside it | — |
| Steer the 3D camera | Middle click aims it at the point; <kbd>Ctrl</kbd>+middle click moves it there; arrow keys drive it | — |
| Look around | — | Right click to start or stop, then move the mouse |
| Orbit | — | <kbd>Alt</kbd>+right drag |
| Move forward and back | — | Wheel, toward the pointer |
| Fly while looking | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |
| Drive | — | <kbd>Up</kbd> forward, <kbd>Down</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right, <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right; while looking, the turn keys strafe |
| Material and surface clicks | — | Middle click: sample the material and copy the surface; <kbd>Ctrl</kbd>+middle click: wrap the copied surface onto one face; <kbd>Shift</kbd>+middle click: paste values onto the hit and the selection; <kbd>Shift</kbd>+<kbd>Alt</kbd>+middle click: paste mapping values only; <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+middle click: wrap mapping only; <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+middle click: project the copied surface onto the hit and the selection; <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Alt</kbd>+middle click: project mapping only |

| Command | Keys |
| --- | --- |
| Duplicate Selection | <kbd>Space</kbd>, <kbd>Shift</kbd>+<kbd>Space</kbd> |
| Delete Selection | <kbd>Delete</kbd>, <kbd>Backspace</kbd>, <kbd>Z</kbd> |
| Select None | <kbd>C</kbd> |
| Invert Selection | <kbd>I</kbd> |
| Next 2D View | <kbd>Ctrl</kbd>+<kbd>Tab</kbd> |
| Top View / Front View / Side View | <kbd>Keypad 7</kbd> / <kbd>Keypad 1</kbd> / <kbd>Keypad 3</kbd> |
| 3D View | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> |
| Maximize or Restore Active View | <kbd>F12</kbd> |
| Frame Selection | <kbd>&#96;</kbd> |
| Carve | <kbd>Shift</kbd>+<kbd>U</kbd> |
| Snap Selection to Grid | <kbd>Ctrl</kbd>+<kbd>G</kbd> |
| Connect Entities | <kbd>Ctrl</kbd>+<kbd>K</kbd> |
| Show Grid | <kbd>0</kbd> |
| Grid 1 to 256 | <kbd>1</kbd> to <kbd>9</kbd> |

**3D Wireframe** has no key. Not implemented yet: Fit Texture (<kbd>Ctrl</kbd>+<kbd>F</kbd>) and Expand Selection (<kbd>Shift</kbd>+<kbd>E</kbd>). Copying or wrapping from a patch, and sampling depth or light colour, are not supported.

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
| Move the selection | <kbd>Alt</kbd>+arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles, or over empty space beside it | — |
| Steer the 3D camera | Middle click aims it at the point; <kbd>Ctrl</kbd>+middle click moves it there; arrow keys drive it | — |
| Look around | — | Right click to start or stop, then move the mouse |
| Move forward and back | — | Wheel, toward the pointer |
| Fly while looking | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |
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
| Carve | <kbd>Shift</kbd>+<kbd>U</kbd> |
| Snap Selection to Grid | <kbd>Ctrl</kbd>+<kbd>G</kbd> |
| Connect Entities | <kbd>Ctrl</kbd>+<kbd>K</kbd> |
| Redo Map Edit | <kbd>Ctrl</kbd>+<kbd>Y</kbd> |
| Show Grid | <kbd>0</kbd> |
| Select All of This Class | <kbd>Shift</kbd>+<kbd>A</kbd> |
| Zoom In / Zoom Out | <kbd>Delete</kbd> / <kbd>Insert</kbd> |
| Grid 1 to 256 | <kbd>1</kbd> to <kbd>9</kbd> |

**Select All** and **3D Wireframe** have no key. Tab keeps moving keyboard focus instead of focusing the camera. Discrete camera steps, floor stepping, right-button selection painting and unique-target cloning are not reproduced. The upstream <kbd>Ctrl</kbd>+middle and <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+middle paste chords are unbound; choose one in **Customize Gestures…** if you want them.

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
| Move the selection | <kbd>Alt</kbd>+arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
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
<kbd>Ctrl</kbd>+<kbd>U</kbd>, fit textures with <kbd>Shift</kbd>+<kbd>B</kbd> and frame selection with
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd>. <kbd>S</kbd> opens Surface Alignment,
<kbd>Shift</kbd>+<kbd>S</kbd> edits patches, <kbd>Shift</kbd>+<kbd>C</kbd> caps patches, and
<kbd>Shift</kbd>+<kbd>T</kbd> toggles texture lock. Shift+arrows shift texture U/V;
Shift+Page Up/Down rotate using the current Surfaces target and step settings.

1.5 area selection adds members instead of toggling them, and surface paste uses the hit face.
Native replacement-area selection, component manipulators, fractional grids, floor stepping,
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

Q3-specific patch, hide/show, select-similar and texture-lock keys are unassigned. Those
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
| Move the selection | <kbd>Alt</kbd>+arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
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
| Move the selection | <kbd>Alt</kbd>+arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
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

**3D Wireframe** has no key. Choosing it does not add Doom 3 or Quake 4 formats, fractional grid steps or a Dark Mod game connection.

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

**Duplicate Selection**, **Select All**, **3D Wireframe** and **Draw Sector** have no key. Use **Edit** > **Duplicate Selection** and **Edit** > **Select All Map Objects** instead. Space-drag, duplicate-on-drag, displacements, Source 2 modes and VMF or RMF files are not supported.

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
| Grid 2 to 256 | <kbd>1</kbd> to <kbd>8</kbd> |

**3D Wireframe** and **Draw Sector** have no key. Grid digits cover 2 to 256 units; 512 and 1024, letter-held scrolling, right-button line drawing and the V, L, S and T modes are not implemented.

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
projection. S opens Surface Alignment and Shift+S opens patch editing. Doom 3
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

- [Package manager](PACKAGE_MANAGER.md): limits, drafts, recovery and every staging rule.
- [Support matrix](SUPPORT_MATRIX.md#archive-and-package-formats): exact format support.
- [CLI strategy](CLI_STRATEGY.md): every `package` option.

## Textures and sprites

The **Textures** page lists the images in a package, decodes them with the
right palette, and opens the Texture Editor for painting, converting and
staging textures for Doom, Quake, Quake II and Quake III projects.

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
one. The viewport is software-rendered, so it needs no OpenGL support:

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
  re-bound. Added geometry takes its weights from the nearest original vertex.
- **Save** keeps the skeleton in the `.mesh.json` source.
- Export as MD5 or IQM to keep the joints, or as MD3, MD2 or MDL to bake the
  animation into frames.

A model with more than 1,024 frames of animation keeps as many whole clips as
fit for editing, and names the rest; the skeleton keeps them all. Joints and
weights cannot be edited yet.

### Edit a mesh

1. Select a model and choose **Mesh Editor** on the page header. With no model
   selected it starts a new mesh; **Open / Import…** in the editor opens any
   format above, or a `.mesh.json` source.
2. Select faces, vertices or edges in the view or the outliner, and edit them
   with the tools below.
3. Choose **Save** to keep an editable `.mesh.json` source. Game files are
   exported separately (see [Export and build game files](manual/models.md#export-and-build-game-files)).

#### Find your way around

The editor is laid out like the Levels page:

- The **outliner** sidebar lists surfaces, frames, tags, collision volumes and
  joints; its **Add** page holds primitives.
- The view sits in the middle, one view or four, with the material row above
  it and the timeline below.
- The property sidebar holds **Item**, **Tool**, **Surface**, **Animation**,
  **Skeleton**, **Collision**, **Quake MDL**, **Export**, **Health** and
  **View**. Choose a tab to open its page and choose it again to close it.

On the **View** page, **Layout** switches between **One view** and **Four
views**, and each of the four panes can show Perspective, Top, Bottom, Front,
Back, Left or Right, named in its corner. Click a pane to make it the one you
work in; it gets an accent border.

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
| Linked geometry | <kbd>L</kbd> under the pointer, <kbd>Ctrl</kbd>+<kbd>L</kbd> from the selection |
| Select all, none, invert | <kbd>A</kbd>, <kbd>Alt</kbd>+<kbd>A</kbd>, <kbd>Ctrl</kbd>+<kbd>I</kbd> |
| Hide selected, hide the rest, reveal | <kbd>H</kbd>, <kbd>Shift</kbd>+<kbd>H</kbd>, <kbd>Alt</kbd>+<kbd>H</kbd> |
| X-ray (select hidden parts) | <kbd>Alt</kbd>+<kbd>Z</kbd> |
| Move, rotate, scale | <kbd>G</kbd>, <kbd>R</kbd>, <kbd>S</kbd> |
| Extrude, inset, duplicate | <kbd>E</kbd>, <kbd>I</kbd>, <kbd>Shift</kbd>+<kbd>D</kbd> |
| Loop cut, bevel vertices, make face | <kbd>Ctrl</kbd>+<kbd>R</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>B</kbd>, <kbd>F</kbd> |
| Proportional editing | <kbd>O</kbd> |
| Four views, maximise the view | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>Q</kbd>, <kbd>Ctrl</kbd>+<kbd>Space</kbd> |
| Show or hide the sidebars | <kbd>N</kbd> |
| Search Mesh Editor commands | <kbd>F3</kbd> |
| Adjust Last Operation | <kbd>F9</kbd> |

</details>

While a transform runs, <kbd>X</kbd>, <kbd>Y</kbd> or <kbd>Z</kbd> constrains it
to an axis, typing a number sets an exact value, <kbd>Ctrl</kbd> snaps and
<kbd>Shift</kbd> slows the pointer. <kbd>Enter</kbd>, <kbd>Space</kbd> or a
click confirms; <kbd>Esc</kbd> or a right-click cancels and leaves the mesh
untouched. With the 3ds Max and MilkShape 3D controls, pick a tool and drag the
selection instead. Every change is one undo step. Changes to the mesh's
structure, such as extrusions and cuts, apply to every animation frame; moves
can apply to **All frames** or the **Current frame**.

The menus (**View**, **Select**, **Add**, **Mesh**, **Vertex**, **Edge**,
**Face** and **UV**) hold the rest: primitives, merging, dissolving, bisecting,
symmetrising, smoothing, solidify, decimation, normals, UV projections, seams
and packing. The property pages cover the details:

- **Item** and **Tool**: exact transforms, pivots and transform axes, and the
  settings of the active tool.
- **Surface**: UVs, seams, materials, **Manage Surfaces…**,
  **Manage Material Slots…** and **Apply .skin File…**.
- **Animation**: frames, named clips, attachment tags and generated in-between
  frames.
- **Skeleton**: joints and skeletal clips (see [Skeletal models](manual/models.md#skeletal-models)).
- **Collision**: static collision boxes (see [Add collision](manual/models.md#add-collision)).
- **Quake MDL**: indexed skins, frame groups, timing and header flags.
- **Export**: the package path, MD2 skin size and level origin used for export
  and staging.
- **Health**: reports duplicate faces, unused vertices, inconsistent winding,
  open boundaries and intersecting faces, with one-step repairs for several of
  them.

The header's export budget button says whether every surface fits the original
MD3, MD2 and MDL limits, and offers **Decimate to MD3 Budget (2,000 Triangles)**
and **Export Quake III Detail Levels…**. Tick **Keep local recovery copies** to
checkpoint unsaved work; **Recover…** restores a copy as an unsaved draft.

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
| Mesh Editor | **Export MD3…**, **Export MD2…**, **Export MDL…** or **Export OBJ Frame…**; **Export Other Format** for **MD5 Mesh…**, **MD5 Animation of Clip…**, **Inter-Quake Model (IQM)…** and **ASE Frame…**; **Stage in Package** or **Stage and Place** for a package and map. |
| Command line | `model build` turns a `.mesh.json` or `.model.json` source into any of those formats; `model lod` writes Quake III detail levels. |

Each export checks the format's limits and reports what it had to leave out:

- MD3 keeps every frame and tag; the original Quake III renderer allows 1,000
  vertices and 2,000 triangles per surface.
- MD2 keeps every frame but needs a single surface (up to 2,048 vertices and
  4,096 triangles), and refuses models with tags.
- MDL keeps indexed skins and frame groups on a single surface (up to 1,024
  vertices and 2,048 triangles), and also refuses tags.
- MD5 mesh and IQM keep the joints and weights; a model without joints gets a
  single origin joint (MD5) or is written as a static mesh (IQM). They animate
  through joints, so edits to frames other than the bind pose do not carry
  over; export MD3 to keep those.
- MD5 animation writes the skeletal clip the current frame belongs to.
- ASE and OBJ write one frame. OBJ has no material library and leaves out
  tags.

MD3 models can be placed in Quake III maps as `misc_model` entities; placing
other models in a level is game-specific and not automated.

### Known limits

- No model format has been tested against real game files in bulk, and no
  MD5, IQM or ASE export has been loaded in the original games yet.
- MDC, MDS, MDM, MDR, Ghoul 2, Half-Life, Hexen II, Heretic II, LightWave and
  KVX models are read only; export them as MD3 or another written format.
- Joints and weights cannot be edited: there is no bone display, pose mode or
  weight painting. Game animation scripts (Jedi Academy `animation.cfg`,
  Wolfenstein scripts, GZDoom MODELDEF) are not read.
- Edge bevel, knife and spin tools and modifiers are not available yet.
- The [modeller release checklist](MODELLER_RELEASE.md) lists what is still
  open.

### Command-line equivalents

```sh
vibestudio --cli model inspect ./id1/pak0.pak progs/player.mdl --json
vibestudio --cli model import ./prop.obj --output ./prop.mesh.json --dry-run --json
vibestudio --cli model build ./prop.mesh.json --output ./build/prop.md5mesh --dry-run --json
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
| Tags, clips and collision | `model tags`, `model animations`, `model collision` |
| Quake MDL skins and timing | `model mdl` |
| Assemblies | `model assembly` |
| Build game files or detail levels | `model build`, `model lod` |
| Controls profiles, and print, check, share or reset controls | `model profiles`, `model controls` |
| Recovery copies | `model recoveries`, `model recover` |
| Place an MD3 in a Quake III map | `map place-model` |

</details>

See [Command line](manual/cli.md) for options and exit codes.

### Learn more

- [Mesh tools](MODEL_TOOLS.md) and [editable meshes](MODEL_MESH.md): every tool, limit and CLI option.
- [Native formats and skeletons](MODEL_FORMATS.md) and [modeller profiles](MODELLER_PROFILES.md).
- [Model design](MODEL_DESIGN.md) and [model assemblies](MODEL_ASSEMBLY.md): props and linked models.
- [Model collision](MODEL_COLLISION.md) and [material slots](MODEL_MATERIAL_SLOTS.md).
- [Modeller release checklist](MODELLER_RELEASE.md): what has and has not been verified.

## Audio

The **Audio** page lists the sounds in a package, draws their waveforms and
plays them. The audio editor cleans up, converts and delivers game-ready sound
effects, and multitrack sessions arrange several sounds into one.

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

The Build page compiles your map with the community's standard compilers, shows each problem where it
happens, and starts the game with the result. This page covers setting up the compilers, running a
build, fixing problems and leaks, and launching a test.

> [!NOTE]
> **Status: Partial.** Build pipelines for Quake, Quake III and Doom maps run ericw-tools, q3map2,
> ZDBSP and ZokumBSP and report problems, leaks and outputs. VibeStudio does not include the
> compilers themselves, and only a few compiler and game combinations have been tested end to end.

### Install the compilers

VibeStudio runs four external compilers. Their source code is kept with the VibeStudio source for
reference and licence review, but VibeStudio builds do not include the compiler programs: install the
ones you need from their own projects.

| Compiler | Used for | Programs |
| --- | --- | --- |
| [ericw-tools](https://github.com/ericwa/ericw-tools) | Quake-family maps | `qbsp`, `vis`, `light` |
| [q3map2 from NetRadiant Custom](https://github.com/Garux/netradiant-custom) | Quake III-family maps | `q3map2` |
| [ZDBSP](https://github.com/rheit/zdbsp) | Doom-family nodes | `zdbsp` |
| [ZokumBSP](https://github.com/zokum-no/zokumbsp) | Doom-family nodes, blockmap and reject | `zokumbsp` |

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
**ericw-tools qbsp** or **q3map2 light**, and whether its tool is ready. **Run Profile**
(<kbd>Ctrl</kbd>+<kbd>R</kbd>) runs the selected step, **Copy CLI**
(<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd>) copies a matching `vibestudio --cli compiler run`
command, and **Copy Manifest** copies the step's command manifest.

### Build a map

1. Open the map on the Levels page.
   The **Input** field on the Build page follows the open map. To build another file, type its path
   or choose it with the folder button beside the field.
2. Choose a **Pipeline**.
3. Choose **Run Pipeline** (<kbd>F7</kbd>).
   The stages run in order, each feeding the next. **Pipeline Stages** shows their progress, and the
   Activity Center keeps the full log.

If the map has unsaved edits, VibeStudio asks first, because the compilers read the file on disk:
**Save** includes your edits, **Build Saved File** builds what is on disk, and **Cancel** stops.
**Cancel** on the Build header stops a running build after the current stage.

| Pipeline | Stages | Input |
| --- | --- | --- |
| **Quake full compile** | QBSP, then VIS and LIGHT (ericw-tools) | `.map` |
| **Quake fast iteration** | QBSP and LIGHT; VIS is skipped | `.map` |
| **Quake BSP only** | QBSP | `.map` |
| **Quake III full compile** | BSP, then VIS and LIGHT (q3map2) | `.map` |
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

For example:

```sh
vibestudio --cli build plan quake-full --input ./maps/start.map
vibestudio --cli build run quake-full --input ./maps/start.map --watch
vibestudio --cli compiler set-path ericw-qbsp --executable /opt/ericw-tools/bin/qbsp
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
> Gemini, local and custom connectors are implemented and tested offline
> against each provider's request and answer formats, but they have not been
> proven by users in real projects. Meshy and voice features are design stubs,
> and agentic workflows are future work.

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

### Use a local model

A local runtime keeps every request on your computer and needs no key.

1. Start your runtime, such as Ollama, LM Studio or llama.cpp's server, and
   make sure it has a model.
2. Open **Settings** > **AI and Automation** and untick **AI-free mode**.
3. Under **Preferred Connectors**, set **Reasoning** to
   **Local/Offline Runtime**.
4. Under **Assistant Model**, pick **Local/Offline Runtime** in **Connector**
   to show its settings, and type the **Model** name exactly as your runtime
   lists it (for Ollama, as `ollama list` shows it).
5. Leave **Endpoint** empty for Ollama's `http://localhost:11434/v1`, or enter
   your runtime's own address.
6. Choose **Test Connection**. It asks the model to reply OK and sends nothing
   from your project.

For pictures, set **Preferred Connectors** > **Image** to
**Local/Offline Runtime**. Under **Image Model**, its **Endpoint** defaults to a
Stable Diffusion web UI, such as AUTOMATIC1111 or Forge, at
`http://127.0.0.1:7860`.

### Use a cloud provider

1. Put your provider's API key in an environment variable for your user
   account (see the table below), then restart VibeStudio so it can read it.
2. Open **Settings** > **AI and Automation**, untick **AI-free mode** and tick
   **Allow cloud AI connectors**.
3. Under **Preferred Connectors**, set **Reasoning** to the provider. Set
   **Image** or **Audio** too if you want generated pictures or sounds.
4. Under **Assistant Model**, pick the same provider in **Connector** and type
   the **Model** name your provider uses. Leave **Endpoint** empty to use the
   provider's own address. The **Key** line says whether the variable was found.
5. Choose **Test Connection**.

| Connector | What it does | Key variable |
| --- | --- | --- |
| **OpenAI** | Text questions and pictures | `OPENAI_API_KEY` |
| **Claude** | Text questions | `ANTHROPIC_API_KEY` |
| **Gemini** | Text questions and pictures | `GEMINI_API_KEY` |
| **ElevenLabs** | Sound effects | `ELEVENLABS_API_KEY` |
| **Custom HTTP Connector** | Any OpenAI-compatible text endpoint, such as a team proxy, and sound endpoints that take ElevenLabs' request | None |
| **Local/Offline Runtime** | Text and pictures from programs on your computer | None |

**Meshy** appears in some connector lists, but it is not usable yet.
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
readable by default, or the raw request body.

The first time a project sends to an endpoint off your computer, VibeStudio
shows the request in a **Send to …?** dialog, where **Cancel** is the default
and **Send** sends it. The box below the request,
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
| **Generate Level…** | **Generate** on **Levels** | Rules on your computer plan the rooms | The text model plans the rooms, and the plan is checked and repaired |
| **Edit Map with AI…** | **Edit with AI** on **Levels** | **Load Proposal…** replays a saved proposal | The text model proposes edits to the open Quake-family map |
| **Generate Texture…** | **Generate** on **Textures** | Starts from a picture of your own | The image model draws variants |
| **Generate Sound…** | **Generate** on **Audio** | The built-in synthesizer makes the sound | The sound model makes it |

Nothing is written or applied until you choose it:

- **Generate Level** shows a layout preview, the plan and its notes.
  **Open in Levels** opens the result as a new, unsaved map; **Save As…** and
  **Save Plan…** write files.
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

Every generator has **Preview Request…** and asks before its first request to an
endpoint off your computer, like the Assistant. Saved textures and sounds
record what made them in the project's `.vibestudio/generated` folder.

### When a request cannot be sent

The Assistant and **Test Connection** say what is missing:

| Message | What to do |
| --- | --- |
| AI-free mode is on. | Untick **AI-free mode** in **Settings** > **AI and Automation**. |
| No connector is chosen. | Pick a **Reasoning** or **Local** connector. |
| … has no endpoint. | Enter the **Endpoint**; a custom connector has no default. |
| … sends to …, off this machine, and cloud connectors are not allowed. | Tick **Allow cloud AI connectors**, or use a local runtime. |
| No model is set for … | Type the **Model** name. |
| … needs an API key in the … environment variable, which is not set. | Set the variable, then restart VibeStudio. |
| This project turns AI off in its manifest. | Run `project init` with `--project-ai-free off`, as shown under [AI-free mode](manual/ai.md#ai-free-mode). |

### Privacy at a glance

- AI is off until you untick **AI-free mode**, and a project can keep it off.
- Cloud providers also need **Allow cloud AI connectors**.
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

<details>
<summary>All AI and generator commands</summary>

| Task | Command |
| --- | --- |
| Show AI settings, keys found and connectors | `ai status`, `ai connectors` |
| Check the text model answers | `ai test-connection` |
| Ask a question, with files as context | `ai ask` |
| Draw or edit pictures | `ai image` |
| Generate or plan a level | `map generate`, `map plan` |
| Propose and apply map edits | `map ai-edit` |
| Generate a texture, or its companion maps | `texture generate`, `texture derive` |
| Generate a sound effect | `asset audio-generate` |

</details>

Settings have matching options, such as `--set-ai-free off`,
`--set-ai-cloud on`, `--set-ai-reasoning <connector>` and
`--set-ai-model <connector>=<model>`. The other `ai` commands, such as
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
  focus.
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

VibeStudio includes a full command line that runs the same services as the studio. Use it to script
package checks, builds and conversions, to run VibeStudio in CI, or to inspect files without opening
the studio. This page shows how to run it, the options every command shares, and how to use its
output in scripts.

> [!NOTE]
> **Status: Available.** The command line covers every studio area, from projects and packages to
> maps, models, builds and diagnostics, and shares its services and tests with the studio. Like the
> rest of VibeStudio, it has had little real-world use so far.

### Run the command line

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
| `install` | Game installation profiles and Steam and GOG detection |
| `workspace` | `.vibeworkspace` files |
| `package` | Inspecting, extracting, validating, comparing, staging and saving packages, drafts and recovery |
| `asset` | Format capabilities, image conversion, audio editing, and project text search and replace |
| `map` | Inspecting, editing, rendering and generating maps, their textures and dependencies |
| `entity` | Entity definition catalogues and map entity validation |
| `editor` | Editor profiles, gestures, keys, view layout, saved views and map layers |
| `model` | Inspecting, importing, editing, building and exporting models |
| `texture` | Decoding, creating, editing, exporting, staging and generating textures |
| `shader` | Quake III shader scripts |
| `material` | Textures, shaders and materials of idTech 1 to 4: list, check, render, edit |
| `sprite` | Doom and Quake sprite plans |
| `code` | Code file lists, local language servers and format-preserving text saves |
| `compiler` | Compiler discovery, paths, single compiler runs and command manifests |
| `build` | Build pipelines and prepared builds |
| `bsp` | Compiled map inspection |
| `launch` | Game launch plans and launching |
| `localization` | Language targets and translation catalogue status |
| `accessibility` | Accessibility settings, speech voices and a speech test |
| `diagnostics` | Support bundles and crash reports |
| `ai` | Optional AI: status, connectors and reviewable proposals |
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
`front` or `side`; without `--output` the SVG goes to standard output.

```sh
vibestudio --cli map render ./maps/start.map --output ./start.svg
vibestudio --cli map render ./maps/start.map --projection front --leak ./maps/start.pts --output ./start-leak.svg --overwrite
```

Plan a build without running anything, then run it with live progress:

```sh
vibestudio --cli build list
vibestudio --cli build plan quake-full --input ./maps/start.map
vibestudio --cli build run quake-full --input ./maps/start.map --watch
```

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

More examples are on the pages for each area, such as [Packages](manual/packages.md) and
[Build and launch](manual/build-and-launch.md).

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
  the open project and package, the log file's path, the number of crash reports, where each compiler
  was found, any keyboard shortcut conflicts and the last 60 lines of the session log.
- From the command line, run `vibestudio --cli diagnostics bundle --output ./diagnostics`. It writes
  `vibestudio-diagnostics.json` with the version, Qt and system details, and the studio's command,
  module, operation-state, interface and localization reports. It leaves out secrets, API keys,
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

VibeStudio does not include the compilers; install ericw-tools, q3map2, ZDBSP or ZokumBSP yourself.
Then open the **Toolchain** tab on the Build page: a tool that shows **Not found** needs
**Locate…**, and **Rescan** looks again after you install one. On macOS and Linux, the program must
be executable. Because `vis` and `light` are common program names, check that **Executable** points
at the ericw-tools programs and not something else on your PATH. If **Path from** says
**Project manifest (missing)**, fix `compilerToolOverrides` in the project's manifest.
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

Detection only reads, and installation profiles start read-only. VibeStudio writes into a game folder
only for test-map copies you allow and prepared-build deployments you review, though the game itself
may write its own configuration and logs when you launch it. Removing a profile never deletes game
files.

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

The submodules hold the source of ericw-tools, q3map2, ZDBSP and ZokumBSP for reference and licence
review. The build does not compile them, but one of the tests checks that they are present.

### Configure, build and test

```sh
meson setup builddir --backend ninja
meson compile -C builddir
meson test -C builddir --print-errorlogs
```

Meson's default build type is `debug`, which runs noticeably slower. CI configures with
`--buildtype=debugoptimized`, which you can add to `meson setup` too. On Windows, Qt's `bin` folder
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
