# Install VibeStudio

VibeStudio runs on Windows, macOS and Linux. This page shows you how to
download a release, check it, install it, run its command line, and remove it
again.

> [!IMPORTANT]
> Every build is pre-alpha, and builds are not signed yet. Download VibeStudio
> only from the project's
> [Releases page](https://github.com/themuffinator/VibeStudio/releases), and
> check each file against `SHA256SUMS.txt` before you run it.

## Download a release

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
[3D views stay empty](troubleshooting.md#3d-views-stay-empty).

## Verify your download

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

## Install on Windows

### Use the installer

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

### Use the portable zip

1. Extract `VibeStudio-<version>-windows-x64-portable.zip` to any folder you
   can write to.
2. Run `bin\vibestudio.exe` inside that folder.
3. If SmartScreen appears, select **More info**, then **Run anyway**.

A portable copy keeps your preferences in the same place as an installed one.
To keep them beside the program instead, see
[Use a settings file of your own](#use-a-settings-file-of-your-own).

## Install on macOS

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

## Install on Linux

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

## Run the command line

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
`vibestudio` with the command for your system. [Command line](cli.md) explains
the commands, JSON output and exit codes.

## Try a nightly build

Nightly builds come from the **release-nightly** workflow on GitHub Actions.
They have the newest changes and even less testing than releases.

1. Open the
   [release-nightly workflow](https://github.com/themuffinator/VibeStudio/actions/workflows/release-nightly.yml)
   and select a successful run.
2. Download the artifact for your system from the run's **Artifacts** list.
   You need to be signed in to a GitHub account to download artifacts.

To build VibeStudio yourself, see [Building from source](building-from-source.md).

## Where VibeStudio keeps your settings

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

### Use a settings file of your own

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

## Uninstall

- **Windows installer:** open **Settings** > **Apps** > **Installed apps**,
  find **VibeStudio**, and choose **Uninstall**. On Windows 10 the list is
  under **Settings** > **Apps** > **Apps & features**.
- **Windows portable zip:** delete the folder you extracted.
- **macOS:** quit VibeStudio and move it from **Applications** to the Bin.
- **Linux:** delete the AppImage file.

For a clean slate, also delete the preferences, logs and recovery folders
listed in [Where VibeStudio keeps your settings](#where-vibestudio-keeps-your-settings).
Recovery copies can hold unsaved work, so check **File** > **Recover Maps…**
and **File** > **Recover Packages…** first.
