# Packaging

VibeStudio packages start as portable release directories (this page), which
the release workflow turns into native downloads. [Releasing](RELEASING.md)
covers versions, the changelog and publishing; this page covers what goes
inside a package.

| Download | Built by | Contents |
| --- | --- | --- |
| `VibeStudio-<v>-windows-x64-setup.exe` | `scripts/package_windows_installer.py` (Inno Setup 6) | The verified Windows package below, installed per user by default, with Start menu entries for the studio and its documentation |
| `VibeStudio-<v>-windows-x64-portable.zip` | `scripts/package_windows_release.py` | The verified Windows package: binary, Qt and audio runtime, notices |
| `VibeStudio-<v>-windows-x64-runtime-source.zip` | `scripts/package_windows_release.py` | Source companion for the bundled runtime |
| `VibeStudio-<v>-macos-arm64.dmg` | `scripts/package_macos_app.py` | `VibeStudio.app` (macdeployqt, ad-hoc signed) with the portable layout under `Contents/Resources` |
| `VibeStudio-<v>-linux-x86_64.AppImage` | `scripts/package_appimage.py` (linuxdeploy) | `meson install` layout plus docs, samples and notices under `usr/share` |
| `VibeStudio-<v>-docs.zip` | `scripts/build_docs_site.py` | The HTML user manual on its own |
| `VibeStudio-<v>-source.tar.gz` | `scripts/package_source_tarball.py` | Every tracked file, compiler submodules included |

Every package contains the HTML manual at `docs/html/index.html` (pass
`--docs-site` to the packaging scripts). Builds are not code-signed or
notarised yet, and clean-machine acceptance of the installers remains open.

## Portable Package Script

`scripts/package_portable.py` stages a versioned package directory containing:

- the built `vibestudio` executable under `bin/`;
- `README.md`, `VERSION`, repository documentation, and the generated
  `docs/OFFLINE_USER_GUIDE.md`;
- `i18n/` with seed Qt TS catalogs for the 20-language target set plus
  pseudo-localization, and the corresponding compiled QM catalogs available
  beside the selected Meson build's executable;
- `samples/` with license-clean Doom, Quake, and Quake III-family projects;
- `licenses/THIRD_PARTY_LICENSES.md` plus VibeStudio license/credits files and
  imported compiler source licenses for VibeMap2, VibeMap3, ZDBSP, and
  ZokumBSP under `licenses/external/compilers/` (VibeMap2 and VibeMap3 as
  `VibeMap2/COPYING` and `VibeMap3/COPYING`);
- the pinned r8brain-free-src MIT and Ooura FFT licences, attribution, and
  source hash manifest under `licenses/external/audio/r8brain-free-src/`;
- pinned libebur128 MIT, R128Scan MIT and BSD-3-Clause queue notices, integration
  notes and source hashes under `licenses/external/audio/libebur128/`;
- pinned PortAudio MIT-style license, integration notes and original-source
  hashes under `licenses/external/audio/portaudio/`; the source companion also
  includes checked host patches and their Meson build rules;
- the pinned Vulkan-Headers `LICENSE.md`, Apache-2.0 and MIT texts, integration
  notes and header hashes under `licenses/external/graphics/vulkan-headers/`.
  No Vulkan loader, MoltenVK or OpenGL driver is bundled: the 3D renderer opens
  the system's at run time, and a machine with neither runs with empty 3D views
  that say why;
- `platform/README.txt` with target-specific launch and clean-machine smoke
  notes;
- `package-manifest.json` schema version 2 with target platform,
  architecture, host platform, binary checksum, docs, source and compiled localization catalogs,
  samples, license bundle, Qt deployment notes, and compiler-bundling status;
- `CHECKSUMS.sha256` covering staged release files.

PR CI uploads current-platform artifacts with 14-day retention; the manual and
scheduled `release-nightly` workflow retains them for 30 days. Windows jobs are
configured to produce a verified binary/source pair with the selected Qt/Audio
runtime and original notices. macOS/Linux jobs still stage portable skeletons
that require their native deployment steps. Hosted execution of these workflow
changes must be checked separately from local pipeline validation. Signing,
installers, clean-machine/native acceptance and durable release publication
remain separate release requirements.

Create the current-platform package:

```sh
python scripts/package_portable.py --binary builddir/src/vibestudio --archive
```

Create all target-shaped package directories from the available binary:

```sh
python scripts/package_portable.py --binary builddir/src/vibestudio --archive --target-platform all
```

On Windows, use `builddir/src/vibestudio.exe` for the binary path.

### Runtime Translation Catalogs

Build with Qt Linguist's `lrelease` available before packaging. Meson writes
the runtime catalogs to `builddir/i18n`; the packager automatically discovers
that directory relative to `builddir/src/vibestudio`. For release validation,
require the entire compiled set explicitly:

```sh
python scripts/package_portable.py --binary builddir/src/vibestudio --compiled-translations builddir/i18n --archive
```

The explicit directory must contain a `.qm` for every application `.ts` catalog,
including English plural forms and pseudo-localization. Missing catalogs fail
before an existing package is replaced. PR and nightly artifact steps require
this complete set. This checks file availability, not translation completeness:
most non-English catalogs still contain unfinished messages.

The manifest's `compiledLocalization` records `status` (`complete`, `partial`,
or `unavailable`), `sourceDirectory`, whether the set was `required`, and the
`catalogs` and `missingCatalogs` lists. Default discovery permits a package with
missing catalogs for development; `.ts` sources alone do not enable runtime
translations or English plural forms. Only catalogs from the selected build or
explicit directory are copied; stale source-tree `.qm` files are excluded.
Compiled catalogs are covered by the package checksums and archive. Qt's own
translations and libraries still require the platform deployment step.

Package replacement rejects linked/reparse-point output directories, nested
output links, linked ZIP destinations, and destinations containing its binary
or compiled-catalog inputs. Existing inputs and linked targets are preserved.

## Validation

### Windows Qt and Audio Runtime

Use the exact Qt SDK that built the executable. `collect_qt_runtime_licenses.py`
downloads only original licence documents from a pinned Qt release, verifies
their Git blob hashes, and keeps every differing module-specific notice.
`deploy_windows_runtime.py` consumes that local, hash-recorded collection;
deployment itself makes no network requests.

Run from the repository root, selecting project-local output directories:

```powershell
python scripts/collect_qt_runtime_licenses.py --qt-version 6.10.1 --output .agents/tmp/release/qt-licenses
python scripts/package_portable.py --binary builddir/src/vibestudio.exe --compiled-translations builddir/i18n --output .agents/tmp/release/packages --target-platform windows --target-architecture x86_64
python scripts/deploy_windows_runtime.py --package .agents/tmp/release/packages/vibestudio-0.1.0-rc1-win64-x86_64 --qt-prefix C:/Qt/6.10.1/msvc2022_64 --license-texts .agents/tmp/release/qt-licenses --archive
```

Substitute the actual package version/directory and Qt prefix. Keep each licence
output directory new, and stage the package without `--archive`; runtime
deployment requires a fresh `bin` containing only the recorded application.
It rejects existing archives, linked outputs, mismatched binaries, unknown
runtime sources and missing or altered notices before copying dependencies.
Native Windows, Qt's deployment/Linguist tools and SPDX JSON inventories are
required; the exercised SDK is Qt 6.10.1 MSVC x64. This does not raise the
application's Qt minimum or replace native macOS/Linux deployment.

The raster Qt Widgets/Audio profile uses `windeployqt` for discovery with an
isolated search path. It copies independent, unchanged files only from the
selected SDK. Windows ICU remains an OS prerequisite; compiler runtimes,
ambient Vulkan SDK files, shader compilers and software OpenGL are not copied.
The current studio uses painted Widgets; future GPU surfaces must extend this
profile with their dependencies and notices. Install the official Microsoft
Visual C++ Redistributable required by the SDK and use its supported Windows
versions. No installer or redistributable is executed by these scripts.

`licenses/qt-runtime/` retains the original Qt SPDX records, source inventories,
available build configuration, component copyrights, full licence texts and
source/hash provenance. Qt DLLs are checked against vendor SPDX hashes,
accounting for the PE signing envelope when needed; their original signed bytes
are copied and recorded with SHA-256. This comparison is not Authenticode trust
verification. Qt's FFmpeg entries have no per-DLL vendor hashes, so the report
states that limit and records SDK-copy hashes, the reported release, each
library's ABI, licence and matching build configuration. Unknown dependencies
fail explicitly. Full notice text does not establish source-distribution or
publication acceptance.

The helper merges checked Qt base/Multimedia catalogs into native-widget
`qtbase_<locale>.qm` files, matching the application's native-catalog loader,
and records their input/output hashes. Application
catalogs remain those selected from the Meson build. Optional
`--include-offscreen` adds the platform plugin used for unattended runtime
tests; it does not substitute for native accessibility or device testing.
The final manifest, checksum file and optional ZIP include all runtime additions.
`qt-runtime-smoke` covers signing envelopes, tampering, dependency closure,
copyright/text preservation, notice variants and source/output isolation.

The [Audio release audit](plans/audio-editor-release-candidate.md) records the
local deployment results and outstanding native, source-distribution and
clean-machine gates. The deployment/source conventions are credited in
[Runtime Distribution](CREDITS.md#runtime-distribution).

The local Audio audit has assembled the exact application capture, ten pinned
runtime/build-tool source archives and build instructions into a verified
companion ZIP. A clean application rebuild passes its selected Audio suites,
catalog comparisons and fixture analysis. Published archive hashes, pinned
recipes and SDK source-SPDX comparisons are retained in the task evidence.
Qt's zlib recipe removes the `unistd.h` include and a linker base-address option
in separate build copies; these adjustments accompany the source/build
instructions. Vendor runtime reconstruction and publication beside the matching
binary still need the combined release check described in the Audio audit.

### Source Companion

`collect_qt_runtime_sources.py` acquires archives from a reviewed, versioned
profile in `scripts/runtime_sources/`. Each archive has an exact HTTPS URL,
byte size, SHA-256 and source revision. `--cached-archives` permits independent
copies of previously verified local archives. The default profile includes
the five deployed Qt 6.10.1 modules, Qt5 build recipes, FFmpeg 7.1.2, zlib 1.3.1,
and QtTools/QtShaderTools source for the translation and shader build tools.
No additional application library is linked. Collection produces a success
receipt only after every archive and the profile itself pass their checks.

`package_source_companion.py` binds these runtime sources to an immutable
application capture and the built portable package. Provide a build-evidence
JSON object containing `sourceHashes` (portable relative filenames to SHA-256),
`binarySha256`, `compiledCatalogs` (QM filenames to SHA-256), `buildType`,
`audioPlayback`, Meson's `compilers` introspection object and a `checks` entry
with `name: "configure"` and the exact configuration argument list in `command`.
Capture the source inventory before a clean build and verify it remains equal
after the build. Keep that evidence with the build logs. The packager verifies
the supplied association; it cannot infer which sources an unrecorded compiler
invocation used. Never create a source attestation from a moving checkout after
building an older revision.

```powershell
python scripts/collect_qt_runtime_sources.py --output .agents/tmp/release/runtime-sources
python scripts/package_source_companion.py --source-root .agents/tmp/release/source-capture --build-evidence .agents/tmp/release/build-evidence.json --binary-package .agents/tmp/release/packages/vibestudio-0.1.0-rc1-win64-x86_64 --runtime-sources .agents/tmp/release/runtime-sources --output .agents/tmp/release/vibestudio-source-companion --archive
```

The packager checks complete source-directory coverage, package checksums,
executable/catalog identity, Qt module versions, FFmpeg configuration, runtime
archives and original notices before writing. It checks input stability again
after independent copies. Existing outputs, source overlap, links/reparse
points, ambiguous portable paths, changed inputs and missing build metadata
fail explicitly. Interrupted assemblies retain `INCOMPLETE.txt` and have no
successful companion manifest. A complete folder has `source-companion.json`
and `CHECKSUMS.sha256`; optional ZIP output is checked member by member and
receives its own SHA-256 sidecar. Progress is written to stderr and the result
as JSON to stdout. Neither tool executes downloaded code or changes a binary
package. See [source build instructions](RUNTIME_SOURCE_BUILD.md).

`source-companion-smoke` exercises byte preservation, source/binary/catalog
binding, tampering, missing/extra files, late changes, output preservation and
portable path/link handling on Windows and Linux. A source bundle still needs
reconstruction/build verification and publication beside the matching binary;
assembly alone does not close native acceptance or the release-channel gate.

### Recorded Builds And Windows Release Pairs

`build_release.py` runs a fresh canonical Meson configure and compile with
warnings as errors and an explicit playback mode. It inventories application
inputs before configuration and requires the same files and bytes afterward.
It also checks the complete compiled catalog set, records compiler/build options
and, with `--qt-prefix`, verifies the actual Qt import-library paths and hashes
from Meson's dependency introspection. Workflow files and imported compiler
notices are recorded as auxiliary inputs. Existing build/evidence directories
are rejected; failed commands retain logs and `INCOMPLETE.txt` without a
successful `build-evidence.json`. Tests remain a separate required CI step.

For the exercised Windows Qt 6.10.1 MSVC x64 profile:

```powershell
python scripts/build_release.py --build-dir builddir --evidence artifacts/build-evidence --buildtype release --audio-playback enabled --qt-prefix C:/Qt/6.10.1/msvc2022_64
meson test -C builddir --print-errorlogs
python scripts/package_windows_release.py --build-dir builddir --build-evidence artifacts/build-evidence/build-evidence.json --qt-prefix C:/Qt/6.10.1/msvc2022_64 --output artifacts/windows-release
```

The release builder records `audio_duplex` alongside playback. Its
`--audio-duplex enabled|disabled|auto` override defaults to the explicit playback
mode, so a playback-disabled release also disables native recording unless
requested separately. Meson-only device-free builds pass both
`-Daudio_playback=disabled -Daudio_duplex=disabled`.

Run each command only after the preceding command succeeds. For automation,
use the workflow's separate steps and explicit failure propagation. The
packager accepts `--source-root` for an independent source capture, provided it
matches the build evidence. Optional `--license-texts` and `--runtime-sources`
reuse previously verified collections; otherwise the pinned collectors run
under the fresh output directory. No downloaded recipe or installer is run.

The packager rejects changed source, binary, catalogs, SDK or compiler notices
before deployment. It runs the packaged executable with the SDK removed from
the DLL search path, using Qt's offscreen plugin and an original PCM fixture to
check version and Audio statistics. It then creates the runtime-bearing binary
ZIP and matching source ZIP, verifies every member, and copies the final pair
to `publish/`. Each ZIP has a SHA-256 sidecar. `release-set.json` binds both ZIPs
to the application executable, source manifest, build evidence and runtime
profile; `build-evidence.json` accompanies it. Failed runs have no successful
release-set receipt, and CI uploads the pair only after the step succeeds.

The Windows workflow uses the existing install-qt action's documented
`QT_ROOT_DIR` to select the same SDK for build and deployment. The Windows
diagnostic artifact retains build logs and the `runtime-checks/` logs/fixture
separately, including on failure. The receipt's runtime log names refer to that
directory. `release-pipeline-smoke` checks fresh
build identity, changed inputs, command failure propagation, SDK isolation and
paired-artifact failure boundaries. Local or CI artifact retention is not a
durable corresponding-source release channel; publish the binary and matching
source together only after the remaining release gates are satisfied.

### Repository and Artifact Checks

Regenerate the offline guide:

```sh
python scripts/generate_offline_guide.py
```

Check that the committed offline guide is current:

```sh
python scripts/generate_offline_guide.py --check
```

Smoke-test current-platform packaging:

```sh
python scripts/validate_packaging.py --binary builddir/src/vibestudio
```

Pass `--compiled-translations builddir/i18n` to require all runtime catalogs.
The validator compares compiled bytes in the staged directory and ZIP with the
selected build, and verifies their checksum entries. `package-translations-smoke`
exercises missing/partial catalogs, explicit overrides and preservation on
invalid output paths; it does not claim that catalogs are fully translated.

Smoke-test the built CLI, including package staging, manifest export, and
deterministic save-as validation for simple PAK and PK3 outputs:

```sh
python scripts/validate_build.py --binary builddir/src/vibestudio --expected-version 0.1.0-rc1
```

Run the MVP release asset gate:

```sh
python scripts/validate_release_assets.py --binary builddir/src/vibestudio
```

The release asset validator checks all three target package manifests, zip
archives, offline guide freshness, localization catalog bundling, license
bundles, checksums, sample/package fixtures for folder/PAK/WAD/PK3,
visible-feedback JSON for package extraction, compiler task-state JSON, AI
workflow JSON, dry-run translation extraction, startup timing, package-open
timing, and static release UX coverage signals.

## Remaining Production Release Work

- Execute the explicit Windows Qt runtime profile above, or native macOS/Linux
  deployment tools, then verify native behavior on clean supported systems.
- Provide corresponding source and build information through the release
  distribution channel; a notice bundle and upstream links alone do not close
  that publication gate.
- Add signing, notarization, published checksums, release notes, and long-term
  artifact retention.
- Decide whether external compiler executables are downloaded, bundled, or kept
  separate per release channel.
- Add package update metadata after release channels are defined.
