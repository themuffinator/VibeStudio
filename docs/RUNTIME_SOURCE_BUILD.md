# Building from a release source companion

A companion's `source-companion.json` identifies the exact application binary,
captured application inputs, compiler versions, Meson options, runtime source
profile and original archive hashes. `CHECKSUMS.sha256` covers its complete
contents. A ZIP also has a separate SHA-256 sidecar. Keep the source companion
available alongside the binary release through the same distribution channel.

The `vibestudio/` directory contains the captured application source, build
files, tests, sample fixtures, catalogs and in-tree dependencies with their
original notices. It is intentionally the source of the recorded executable;
later documentation and packaging helpers live outside that directory. The
outer `scripts/` directory contains the source-companion tooling itself.
Its fixture tests can be run with `python -B src/tests/source_companion_test.py`;
set `VIBESTUDIO_TEST_TMP_ROOT` to a disposable project-local test directory.
`binary-notices/` preserves the release's original licence/SPDX/build records.
External compiler executables are not bundled in this profile and remain
separate tools; their source repositories are recorded in `.gitmodules`.

## Application build

Use Python 3, Meson, Ninja, a C++20/C compiler and the Qt development SDK for the
target platform. Windows uses an x64 MSVC-compatible development environment;
the audited executable uses Clang 20.1.7 with Qt 6.10.1 MSVC. The receipt records
the actual compiler and configuration rather than substituting a newer release.

From the companion directory, put build output in an explicitly selected
project-local directory outside `vibestudio/`, then run the canonical build:

```powershell
meson setup .agents/tmp/rebuild/build vibestudio --backend=ninja --buildtype=release --vsenv -Db_vscrt=md -Daudio_playback=enabled
meson compile -C .agents/tmp/rebuild/build
meson test -C .agents/tmp/rebuild/build --no-rebuild audio-project-smoke audio-decode-smoke audio-loudness-smoke
```

Set `CC`/`CXX` to the selected compiler and make the matching Qt `bin` directory
available to Meson. Apply any additional `buildOptions` recorded in the receipt.
On Linux/macOS, omit `--vsenv` and `-Db_vscrt=md` and select the native toolchain.
An `audio_playback=disabled` build retains editing, analysis, import/export and
CLI operations without Qt Multimedia. Rebuilding at a different absolute path
may change executable bytes; the recorded source hashes establish input identity,
not a claim of bit-for-bit vendor or application reproducibility.

## Qt, FFmpeg and zlib

`runtime/archives/` contains unchanged source archives. The reviewed Windows
profile includes QtBase, Multimedia, SVG, ImageFormats and Translations 6.10.1;
QtTools supplies Linguist and QtShaderTools supplies `qsb`. The Qt5 archive
contains the matching top-level build and provisioning recipes. Its submodule
directories are empty until populated from the individual module archives.
FFmpeg n7.1.2 and zlib 1.3.1 are separately pinned.

Extract each archive into a fresh, short project-local source directory. Place
the seven Qt module roots under the Qt5 root using their module names
(`qtbase`, `qtmultimedia`, `qtsvg`, `qtimageformats`, `qttranslations`, `qttools`,
`qtshadertools`). Preserve the original archives. Follow the included Qt build
scripts with a supported MSVC/Windows SDK, CMake, Ninja and Python environment;
use an explicit project-local installation prefix. Qt's
[Windows build documentation](https://doc.qt.io/qt-6.10/windows-building.html)
describes the configure, build and install phases.

The release retains each module's `config_*.opt` and `config_*.summary` under
`binary-notices/licenses/qt-runtime/build/`. Those are original vendor records,
including vendor machine paths and optional CI services. Relocate paths and
choose available toolchain/cache services explicitly; do not execute the
provisioning scripts as a local installer. Their source and headers remain
unchanged reference material.

Build zlib before FFmpeg in an independent x64 build copy. The retained Qt
`coin/provisioning/common/windows/zlib.ps1` recipe removes the `unistd.h`
include from `zconf.h` and the `-base:0x...` linker flag from `win32/Makefile.msc`,
then uses `nmake /f win32/Makefile.msc`. Preserve the original source and record
these two changes in the build copy. The recipe and its helpers are included
with their original LGPL-3.0/GPL-3.0 licence alternatives.

FFmpeg's exact deployed configuration is in `source-companion.json`. Its Qt
recipe and `ffmpeg_config_options.txt` are in the same provisioning tree.
Use the recorded shared MSVC configuration and zlib include/library paths,
relocated into the selected build directory. FFmpeg's
[MSVC build instructions](https://ffmpeg.org/platform.html#Microsoft-Visual-C_002b_002b-or-Intel-C_002b_002b-Compiler-for-Windows)
describe the MSYS2/NASM environment. Build with GNU Make, preserve the recorded
disabled features and enable the same zlib dependency. Qt's recipe also renames
MSVC import libraries after installation. Build QtBase and the Qt tools, then
the remaining modules, pointing Multimedia to the resulting FFmpeg installation.

## Verification and publication

Compare rebuilt library version/configuration reports with the profile and run
the application regressions against the resulting SDK/runtime. Keep native
device, accessibility and clean-machine results tied to the tested executable.
Source assembly and hash checks do not substitute for those runs or for a
verified vendor-runtime rebuild. The Audio release audit records which checks
actually ran and which remain open. See the application source's packaging,
dependency and credit documents for its integration and licence details.

The additional build-tool archives are
[QtTools 6.10.1](https://github.com/qt/qttools/tree/9e0030f889168f7a0ec1bb47a7d7138a497b3c96)
and [QtShaderTools 6.10.1](https://github.com/qt/qtshadertools/tree/86c4b079a05c2dbe5fdb6f46ad9df8ef297487a9),
reviewed 2026-10-05. `lrelease`/`qsb` use GPL-3.0 with Qt-GPL-exception-1.0;
Qt library components offer LGPL-3.0/GPL-3.0 alternatives. Every source archive
retains its original notices. The source profile records the other upstream
URLs, revisions and licence review; no new runtime library is linked.
