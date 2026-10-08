# Build from source

Build VibeStudio yourself to try the latest changes, debug a problem or contribute. You need a C++20
compiler, Qt 6, Meson, Ninja and Python 3; this page lists them, then walks through configuring,
building, testing, running and packaging.

> [!NOTE]
> The project's CI builds and tests every pull request on Windows, macOS and Linux with Qt 6.10.1.
> Other compilers and Qt versions may work, but CI does not check them.

## What you need

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

## Get the source

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

## Configure, build and test

```sh
meson setup builddir --backend ninja
meson compile -C builddir
meson test -C builddir --print-errorlogs
```

Meson's default build type is `debug`, which runs noticeably slower. CI configures with
`--buildtype=debugoptimized`, which you can add to `meson setup` too. On Windows, Qt's `bin` folder
must also be on PATH when the tests run, as in
[Run from the build folder](#run-from-the-build-folder).

## Use the Windows helper script

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

## Build options

Set an option when you configure, for example `meson setup builddir -Daudio_playback=disabled`, or
change it later with `meson configure builddir -Daudio_playback=disabled`.

| Option | Default | Effect |
| --- | --- | --- |
| `audio_playback` | `auto` | Plays sounds through Qt Multimedia when it is installed. Without it, the Audio page still decodes, draws and exports sounds. `enabled` makes a missing Qt Multimedia an error. |
| `audio_duplex` | `auto` | The synchronised recording backend: WASAPI, CoreAudio or ALSA. `disabled` builds without it. |
| `compiler_tools` | `disabled` | Reserved for building the external compilers from their submodules; it has no effect yet. |
| `update_channel` | `dev` | `stable`, `beta` or `dev`, recorded in the build and shown in its reports. |
| `github_repo` | `themuffinator/VibeStudio` | The repository, as owner/name, recorded in the build and shown in its reports. |

## Run from the build folder

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
[Troubleshooting and FAQ](troubleshooting.md#start-with-clean-settings). `--self-test` builds and
paints every page, then exits; CI runs it with the environment variable `QT_QPA_PLATFORM=offscreen`.

## Work in Visual Studio Code

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

## Make a portable package

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
[Releasing](../RELEASING.md#run-the-packaging-steps-locally) explains.

## Learn more

- [Contributing](../CONTRIBUTING.md): task sizing, validation expectations and credits rules.
- [Architecture](../ARCHITECTURE.md): how the studio's modules fit together.
- [Dependencies](../DEPENDENCIES.md): every library and tool the build uses.
- [Packaging](../PACKAGING.md): release packaging and validation.
