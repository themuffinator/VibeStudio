# Project status

VibeStudio is pre-alpha. This page explains what that means, what has and has
not been tested, how VibeStudio protects your files, and the known limitations
in each area.

> [!WARNING]
> Do not use VibeStudio for production work yet. Open copies of your projects,
> maps and packages, and keep your own backups.

## What pre-alpha means here

VibeStudio is still being built. New features arrive often, and existing ones
change. Several parts of the planned studio, such as a sprite animation
editor and agentic AI workflows, do not exist yet. The parts that do exist have
automated tests, but almost none of them have been used by real people on real
projects. Expect rough edges, missing pieces and bugs, and please report what
you find.

## What is tested

Every pull request and every change to the main branch is built and tested on
Windows, macOS and Linux by the project's continuous integration (the **pr-ci**
workflow on GitHub Actions). Each run:

- builds VibeStudio and runs several hundred automated tests: format readers
  and writers, parser fuzzing and deliberately damaged files, saving and
  recovery, command-line commands, theme contrast checks, and GUI tests that
  drive the real studio window with simulated input on an off-screen display;
- checks what the 3D views draw on Linux, with Mesa's software Vulkan driver.
  The Windows and macOS runners have no graphics driver, so those drawing
  checks are skipped there;
- starts the studio in a self-test mode that builds and paints every page;
- checks that the command-line documentation matches the commands the program
  really has;
- validates the sample projects, the documentation links, the credits and
  licence notices, the translation catalogues and the layout of the release
  packages.

## What is not tested yet

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

## Keep your work safe

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

## Known limitations

These are the gaps you are most likely to meet. The
[support matrix](../SUPPORT_MATRIX.md) lists every format and workflow with its
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
| Releases | A release finds what maps, models and shader scripts name; files that game code or scripts load by name must be added with include patterns. Telling the game's files from yours needs the game indexed first. Not yet tried with real game installations. |
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

## Platform notes

- **Builds are not signed.** Windows SmartScreen may show **Windows protected
  your PC**, and macOS Gatekeeper blocks the first launch.
  [Install VibeStudio](install.md) shows how to open them safely.
- **macOS:** builds run on Macs with Apple silicon and macOS 13 or later. There
  is no Intel build.
- **Linux:** AppImage builds do not include in-studio audio playback or
  recording. They need FUSE 2 and glibc 2.35 or newer.
- **Windows:** builds run on 64-bit Windows 10 and 11.

## Report a problem

1. Check [Troubleshooting](troubleshooting.md) for a known fix.
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

## Learn more

- [Roadmap](../ROADMAP.md): milestones and what comes next.
- [Support matrix](../SUPPORT_MATRIX.md): every format and workflow, with its
  exact limits.
