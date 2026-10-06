# Audio editor release-candidate gates

Objective: achieve a release-candidate, professional-grade audio editor for
VibeStudio's idTech game workflows. The initial PCM editor is a foundation;
passing its smoke tests alone does not establish release readiness.

These gates follow the project rules, the Audio roadmap, the shared-service
architecture, and the accessibility, localization, and first-run requirements.
Every unchecked gate remains open. Completion requires current source and
runtime evidence, with unavailable platform or manual checks stated explicitly.

## Documents and editing

- [x] Lossless, versioned native documents preserve samples and editor metadata;
  atomic saves, external-change conflicts, failed saves, and close/open/new
  transitions cannot silently lose work. Export and staging are separate from
  saving an editable document.
- [x] Local recovery copies are bounded, inspectable, and restorable as drafts;
  corrupt records fail safely and restoring never overwrites a source.
- [x] Frame-accurate selection, cursor, zoom, pan, overview, channel displays,
  copy/cut/paste/mix, insert silence, trim/delete, fades, gain, normalization,
  polarity, DC correction, and channel conversion share undoable operations.
- [x] Resampling controls aliasing and preserves duration, channel alignment,
  selection, and metadata; numerical fixtures establish quality.
- [x] Undo/redo covers samples, selection, format, and metadata, with meaningful
  descriptions, bounded memory, cancellation, and accurate saved revisions.
- [x] Loop and cue authoring survives edits and supported game exports, with
  explicit behavior when an edit removes or changes a marker.

## Audio, formats, and integration

- [ ] Edited playback has reliable play/pause/stop, seek, selection/loop audition,
  volume, visible position, backend errors, and coordination with asset previews.
- [x] WAV precision and dither choices, DMX output, game-compatible presets, and
  compressed import meet documented format contracts. Malformed or unsupported
  input fails clearly; bounded decoding and export never damage source assets.
- [x] Analysis exposes clipping, peak/RMS/DC levels, and useful channel/selection
  information. Recording and multitrack requirements are assessed against the
  intended sound/music authoring workflow and remain visible in the roadmap.
- [x] True-peak and integrated loudness metering pass independent signal checks,
  explicit surround-role review, bounded cancellation and both playback builds.
- [x] Audio browser, staged package assets, level sound references, dependency
  review, and package save/test workflows see the same edited output. Stale
  package context cannot receive an unintended handoff.
- [x] CLI shares document, edit, export, validation, and recovery services, with
  useful JSON reports, deterministic dry runs, and tested exit codes.
- [x] Core editing remains complete without cloud AI or a playback device.

## Release verification

- [x] Expensive work stays asynchronous; representative workloads establish
  responsiveness, cancellation latency, memory bounds, and useful progress.
- [x] Independent sample/format fixtures, corrupt inputs, conflicts, recovery,
  history, transport, integration, and CLI tests cover the actual claims.
- [ ] Accessibility, focus and command navigation, 100/200% scale, high-contrast
  dark/light, RTL, and translation expansion are verified. Direct widget calls
  and widget rendering avoid taking control of the user's keyboard or mouse;
  remaining manual interaction checks must be identified.
- [ ] Canonical Meson/Ninja builds and relevant regressions pass, with and
  without optional playback. Windows/macOS/Linux support and available native
  platform evidence are audited without claiming unexecuted checks passed.
- [x] User guide, support matrix, architecture, CLI, stack/dependencies, credits,
  localization, first-run docs, and release notes match the implementation.
- [ ] A final requirement-by-requirement audit proves these gates on the current
  worktree, with no known data-loss or release-blocking correctness defect.

## Requirement evidence

The tests below establish the documented software behavior. Platform results and
remaining acceptance limits are recorded separately; a test's existence does not
mean that every platform or manual workflow has passed.

| Area | Automated evidence | Remaining acceptance |
| --- | --- | --- |
| Native documents and recovery | `audio-project-smoke`, `audio-recovery-smoke`, editor/startup UI tests: exact samples, atomic saves, conflicts, corrupt copies, explicit restore and retained source files. | Final platform matrix and packaging. |
| Editing and history | `audio-clip-smoke`, `audio-resample-smoke`, `audio-markers-smoke`, `audio-history-smoke`: frame ranges, numerical conversion, marker remapping, exact zero bits, quotas, saved revisions, exceptions and cancellation. Optimized Windows and Linux editor workloads pass. | Native desktop and physical interaction acceptance. |
| Import and delivery | Decode, export and delivery fixtures check independent bytes, malformed streams, codec limits, precision, dither, presets, path protection and CLI reports. | Native platform decoder/package deployment. |
| Playback and browser coordination | `audio-transport-smoke` and `audio-browser-smoke`: backend sessions, seek, stop, retry, errors, duplicate occurrences, asynchronous work and float codec preservation. Separate production-adapter probes pass fifteen checks at zero volume on Windows Qt 6.10.1 and WSL Linux Qt 6.4.2, including a second loop's position advance. | Physical listening, device removal/reconnect and other native platform/backend acceptance; WSLg audio is routed through the Windows host. |
| Package and level handoff | Level core/UI and browser tests check reviewed Quake II/III entities, staged samples, undo and changed-context rejection. The generated Audio compiler workflow passes 31 steps through native project edits, draft dependency review, actual Quake II/III compilers and independently inspected PAK/PK3 payloads. | Manual integrated GUI authoring and game listening/testing; other native compiler platforms. |
| Analysis | `audio-analysis-smoke`, `audio-loudness-smoke` and editor UI cover sample statistics, reconstructed peaks, K-weighted/gated loudness, exact selection, speaker roles, cancellation and read-only behavior. Independent FFmpeg loudness and long-sinc peak checks use original fixtures. | Current metering matrix is recorded below; no broadcast-certification claim. Recording/multitrack remain separate roadmap items. |
| Accessibility and localization | UI suites cover names/roles/descriptions, stale-description clearing, focus policy, scalable controls, high contrast, expanded text, RTL and direct widget renders. | Physical keyboard traversal and assistive-technology acceptance. |
| Responsiveness and storage | Full-size history and opt-in editor/browser workloads record event-loop activity, cancellation and bounded input/sample/history storage in optimized Windows and Linux builds. Linux `wait4` records the history process's peak RSS. | Workload observations do not establish a whole-process memory cap or a performance guarantee on other machines. |
| Documentation and distribution | Credits, source layout, documentation, CLI help, translation extraction, English plurals and generated-guide validators pass. Windows and Linux packages include all 21 compiled catalogs. The Windows runtime milestone below adds SDK-only discovery, original notices, unchanged signed DLLs and native Qt catalog loading. The source companion binds the captured application and pinned runtime sources to that package; a clean application rebuild passes. | Vendor runtime reconstruction, corresponding-source publication, clean-machine and native macOS evidence; most application messages in target-language catalogs remain untranslated. |

## Recorded Windows release pipeline — 2026-10-05

Windows PR and nightly workflows now use a fresh, recorded Meson build and
produce a verified binary/source pair. The build recorder captures application,
workflow and compiler-notice inputs, selected Qt import libraries, build options,
commands, logs, executable and compiled catalogs. The packager checks these
inputs again, deploys the reviewed runtime, verifies original stereo PCM through
an isolated CLI probe, and writes the pair receipt only after both archives and
their checksums pass. Workflow diagnostics are uploaded separately from release
artifacts. These workflow edits have been validated locally; hosted Actions and
release publication have **not run**.

A fresh capture contains 1,075 application files and seven auxiliary files.
It includes 205 changed paths and two removals since the preceding 978-file
capture; all 159 previously audited Audio-owned file hashes remain unchanged.
Canonical Meson/Ninja completed 922 steps with Clang 20.1.7, Qt 6.10.1,
warnings as errors, release optimization and playback enabled. All 33 selected
Windows suites pass, covering the previous 31 Audio/shared suites plus release
pipeline and translation packaging. CLI validation passes from the initialized
checkout, and documentation covers all 202 registered commands. The previous
four-configuration matrix retains its older source scope; it was not repeated
for this new capture.

The final release tooling passes all 13 Python fixture methods on Windows and
Ubuntu WSL2, with warnings treated as errors. Tests cover source/SDK mutations,
failed builds and runtime checks, immutable output, source/binary binding,
runtime isolation and independently calculated PCM statistics. The final Linux
Meson run took 301.32 seconds; two earlier 300-second runs timed out while
waiting on the Windows-mounted filesystem. The registered limit is now 900
seconds. No integrity assertion was relaxed. Both workflow YAML files, all 12
PowerShell and 11 Bash blocks, and eight fail-fast validation scenarios pass
local checks.

The packaged executable has SHA-256
`da59fc3a3c975a56bd4cd83c72ddaeb673eb56d851794336338b20264cbbaf30`.
The matching archives are retained under
`.agents/tmp/audio-editor-rc/ci-release-final-deployment/publish/`:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Windows binary ZIP | 41,782,852 | `38c06e525489f7586654f18c3427f54c5b9cfae8fc4a2ecbfcc3b777ecf4fe04` |
| Matching source ZIP | 110,545,652 | `2284638311b1cf4a2038daab25188039c7a96f3d56cb401ac01a4341be2bc035` |

This binary also passes the 31-step Quake II/III compiler/package handoff with
original generated assets. Analysis of the original six-second manual fixture
is identical to the preceding producer's result. Its manual-acceptance manifest
now identifies this binary and still says **not run** for physical acceptance.

The first packaging probe omitted `--cli`, entered the offscreen GUI and timed
out. It created a startup log, marker and empty session metadata in the default
settings store. The verified task-created log and marker were copied to evidence
and removed; existing older logs were preserved. Session metadata was left in
place because no earlier snapshot was available. The corrected probe explicitly
uses CLI mode and private settings; a new regression and the actual packaged
probe pass. No input control, screen capture or listening test was performed.
This probe fix and its test were made after the captured build. Current workflow,
documentation and test-timeout edits are also outside that frozen source set;
the final evidence records each boundary without rewriting the captured inputs.

Consolidated evidence is
`.agents/tmp/audio-editor-rc/evidence/release-pipeline-verification.json`.
Separate build, package, workflow, compiler-handoff and Linux-tooling records
retain exact commands, hashes and failures. Two unrelated diagnostic limitations
remain: a repository-only credits check can discover a parent Git HEAD when
run in a non-checkout source capture, and this Linux Meson version emits an
ignored shutdown warning about an unclosed setup log. The checkout-context
credits check and Meson commands themselves pass.

Vendor Qt/FFmpeg reconstruction, hosted CI, publication, physical listening and
device reconnect, keyboard/screen-reader acceptance, native macOS/Linux desktop
and clean-machine checks remain open. Later shared studio changes also require
a final integrated requirement audit. This milestone does not complete the RC.

## Source companion and application rebuild — 2026-10-05

The captured Windows Audio package now has a verified source companion. The
production collector acquires ten pinned archives: the five deployed Qt 6.10.1
modules, Qt's matching build/provisioning repository, QtTools, QtShaderTools,
FFmpeg n7.1.2 and zlib 1.3.1. The two build-tool archives match official archive
hashes, their recorded commit tags and original licence texts. All ten unchanged
archives total 95,623,775 bytes; collection and packaging do not execute them.
The build guide preserves the vendor configuration and both zlib adjustments.

The source packager binds all 978 application source files, 21 compiled catalogs
and the executable to the recorded clean-build evidence. It verifies the binary
package, runtime module/configuration identity, source archives and original
notices before making independent copies, then rechecks input stability.
Interrupted output has no successful manifest. Complete output has checksums,
build metadata, original notices, source/build instructions and the packaging
tools with their fixture tests. The final ZIP has 1,090 files and 109,852,306
bytes, with SHA-256
`5a70f426883f36ca112feaf9a17a3eeba109bc08b1399f36526321bf307432ca`.
Its producer executable remains
`0599edaa15021a38b17bcf416176a2d801a158b9794bab9d727895b2b1d5a4f8`.

Canonical Meson runs of `source-companion-smoke` pass all 15 test methods on
Windows and Ubuntu WSL2, with Python warnings treated as errors. Windows skips
the unavailable symlink-creation case and exercises junction protection; Linux
exercises symlinks and skips the Windows junction case. Tests cover source,
binary and catalog binding, missing/extra/tampered inputs, late changes,
immutable outputs, path/link rejection, archive size bounds and cache reuse.
The actual tools and test shipped in the final companion also pass on Windows.

A fresh application tree extracted from the first verified companion ZIP builds
with canonical Meson/Ninja, Clang 20.1.7 and Qt 6.10.1, in release mode with
warnings as errors and playback enabled. The Audio document, decoder and
loudness suites pass, all 21 catalogs match the producer byte for byte, and the
original six-second manual fixture produces identical audio analysis. The
rebuilt executable has SHA-256
`ecc97269de8256badf39708a4d81f80a5563df1dafb8805a2c1f6a39c7b81243`;
this establishes application reconstruction, not a byte-identical executable.
The final companion preserves the same 978 application files and runtime
archives. Only its outer packaging helpers, shipped fixture test and build-guide
text changed; the verification record explicitly checks that boundary.

The companion and ZIP are retained under
`.agents/tmp/audio-editor-rc/source-companion-final-deployment/`.
The consolidated record is
`.agents/tmp/audio-editor-rc/evidence/source-companion-verification.json`;
the final archive and rebuild records retain exact inventories, commands and
logs. This milestone changes packaging tooling and documentation, with no
production Audio C++ changes. The earlier four-configuration matrix remains
evidence for the captured application, rather than the moving shared tree.

The later recorded-release milestone above integrates the Windows CI packaging
steps. Vendor Qt/FFmpeg source reconstruction, hosted CI execution and
publication beside the matching binary remain open. Physical listening,
device removal/reconnect, keyboard and screen-reader acceptance, native
macOS/Linux desktop checks and clean-machine testing remain **not run**.
The final requirement audit on the current shared worktree is also open.
The unrelated Levels header registration reported below has since been resolved
by shared work; the current source-layout validator passes.

## Shared-source integration verification — 2026-10-05

A new independent source capture includes the shared package-preview streaming
adapter, newer studio services and the completed Audio metering implementation.
All 978 captured files matched the live tree before and after capture, and
remained unchanged through the four builds. This captures 91 changed production
or test paths since the metering milestone. Later shared studio edits are
recorded separately; these results do not certify the moving worktree.

| Captured optimized configuration | Selected suites |
| --- | --- |
| Windows x64, Clang 20.1.7 / Qt 6.10.1, playback enabled | 31/31 pass |
| Windows x64, playback disabled | 31/31 pass |
| Ubuntu WSL2, GCC 13 / Qt 6.4.2, playback enabled | 31/31 pass |
| Ubuntu WSL2, playback disabled | 31/31 pass |

These are 31 distinct suites and 124 executions, using canonical Meson/Ninja
release builds with warnings as errors. They cover Audio core/editor/browser,
history, import/startup, transport, analysis, package preview/operations,
staging, archive integrity, settings and level/package handoff. Offscreen tests
use direct widget calls and fake audio outputs. Shell interaction tests are
excluded because they synthesize keyboard/mouse events, outside the current
input-control authorization. No physical interaction or listening is inferred.

The Audio browser's streaming adapter now participates in this combined audit.
Existing duplicate-occurrence, CRC, cancellation and bounded-preview fixtures
pass against the shared streaming API. Metering/resampling implementation and
analysis CLI hashes still match the earlier independent numerical checks;
those oracle and workload observations retain their original executable scope
and are not reported as rerun on these binaries.

The Windows package uses the newly built executable, SHA-256
`0599edaa15021a38b17bcf416176a2d801a158b9794bab9d727895b2b1d5a4f8`.
Ten packaging/runtime checks pass, including all 21 application catalogs,
32 native Qt catalogs, deployed plugin loading, decode/loudness CLI checks,
192 registered CLI commands and the 31-step Quake II/III compiler/package
workflow. The 265-file package contains 31 SDK DLLs and the previously reviewed
notices. Every checksum and ZIP member is verified. The SDK and Vulkan SDK
are absent from the runtime search path; clean-machine acceptance remains open.

The consolidated record is
`.agents/tmp/audio-editor-rc/evidence/rc-integration-verification.json`.
Its detailed build/package reports retain commands, exact input/binary hashes,
later worktree differences and exclusions. The manual fixture manifest points
to this executable and remains **not run** for physical acceptance.

Documentation, credits, generated-guide and scoped whitespace checks pass.
At this milestone, the final live source-layout check flagged an unrelated newer Levels header,
`core/level_placement_control_p.h`, missing from Meson's installed-header list.
It was added after this source capture; the failure is retained in
`rc-final-source-layout.txt`. Later shared work resolved that registration, as
verified in the source-companion milestone above. No Levels implementation or
registration is changed by this Audio audit, and final moving-worktree
acceptance remains open.

### Runtime source inputs

Eight unchanged archives are retained under
`.agents/tmp/audio-editor-rc/runtime-source-inputs`: the five selected Qt 6.10.1
modules, Qt's matching build/provisioning repository, FFmpeg n7.1.2 and zlib
1.3.1. They total 84,424,547 bytes. Qt archives match official SHA-256 metadata;
FFmpeg/zlib archives match Qt's pinned recipe checksums and also record SHA-256.
Qt build-recipe members are checked against the pinned Git tree, accounting
for its explicit export exclusions and substituted commit tag.

Four SDK source-SPDX documents contain 26,618 file records: 7,941 match exact
bytes, 18,265 match Windows CRLF checkout bytes, 410 are export-ignored
`.gitignore`/`.gitattributes` files, and two record unexpanded commit placeholders.
There are no unexplained content mismatches. QtTranslations has no SDK source
SPDX document; its official archive hashes and commit tag are checked separately.
Original archives are never rewritten, extracted or executed during this audit.

The retained Qt recipe includes both zlib build adjustments: removing the
`unistd.h` include and the linker base-address option in per-architecture build
copies. Source acquisition/review records are
`runtime-source-inputs-verification.json` and `runtime-source-inputs-review.json`
under the evidence directory. The later source-companion milestone above adds
assembly and a clean application rebuild. Vendor runtime reconstruction,
release-channel integration and publication acceptance remain open. Source
inputs alone do not close that gate.

## Windows runtime distribution verification — 2026-10-05

The portable Audio package now uses `deploy_windows_runtime.py` to discover and
validate dependencies before copying files from the selected Qt SDK. The initial
deployment audit found Windows ICU from `System32` and a shader compiler from an
ambient Vulkan SDK. Discovery now uses an isolated SDK/system search path, records
ICU as an OS prerequisite and excludes shader/software-OpenGL components from the
current raster Widgets profile. It never copies installed files through hard links.

The Qt 6.10.1 SDK's SPDX inventory hashes precede Authenticode signing. Tests
check exact hashes and strictly bounded PE signing-envelope normalization; the
package preserves each signed DLL byte for byte and records its SHA-256. This
compares content provenance, not certificate trust. Qt's FFmpeg inventory lacks
individual DLL checksums, so all five FFmpeg 7.1.2 libraries record exact SDK
hashes plus their matching version, ABI, licence and build-configuration APIs.

Original SPDX copyright/component records, full licence texts, module-specific
text variants, available SDK build configuration and source references accompany
the runtime. The licence collector verified 75 documents against upstream Git
blobs and preserved 49 distinct documents; the selected runtime references 89
component records and 31 licence identifiers. The collector downloads notices
only. Corresponding implementation source/build distribution remains a separate
publication requirement, and this milestone does not certify licence compliance.

The audit also found that the earlier `qt_*.qm` deployment did not match the
app's `qtbase` catalog prefix. Merged QtBase/Multimedia translations now retain
`qtbase_*.qm` filenames. A native Qt Core probe loads all 32 runtime catalogs with
the same locale/prefix lookup as the app, compares six standard buttons with the
original SDK catalogs, and confirms translated Cancel captions. All 21 compiled
application catalogs load; English Audio singular/plural forms remain correct.
This verifies catalog deployment, not completion of unfinished translations.

- Canonical Meson/Ninja release builds with warnings as errors compile the
  catalog probe on Windows and Ubuntu WSL2. Both platforms pass
  `qt-runtime-smoke` (12 methods) and `package-translations-smoke` (8 methods).
  Windows skips one runtime and three catalog symlink cases because symlink
  creation is unavailable; Linux exercises them and skips only the Windows
  junction case. These builds compile the probe, not the entire current studio.
- Eight local Windows package checks pass: staging, deployment, version,
  loudness/CLI, transport/plugin loading, editor UI, decoding/CLI and native
  catalog loading. The package contains 31 SDK DLLs, 32 native Qt catalogs,
  21 application catalogs and 265 files. Every checksum and ZIP member matches.
  Qt's SDK and the Vulkan SDK are absent from the test search path; the deployed
  offscreen and FFmpeg plugins load from the package.
- The application is the previously verified metering binary, SHA-256
  `71f496c820d4b1d9351ff57a0527112dd60e023b3a74c9b836f59fa9855a4587`.
  Later shared studio edits are outside that binary. This runtime milestone
  adds no production C++ changes and does not repeat or extend its four-build
  metering matrix to newer source.

The consolidated evidence is
`.agents/tmp/audio-editor-rc/evidence/runtime-distribution-verification.json`.
Detailed package, probe and tooling records retain commands, source/binary
hashes, test output, original preflight failures and remaining acceptance.
This milestone used the metering executable; the newer shared-source package
above supersedes it for the manual fixture. Physical interaction, listening,
device disconnect/reconnect, native macOS/Linux desktop and clean-machine
acceptance remain **not run**. Corresponding-source publication also remains open.

## True-peak and loudness verification — 2026-10-05

The editor and CLI now share read-only integrated loudness and true peak.
libebur128 1.2.6 supplies K-weighting, complete metering blocks and absolute/
relative gates. A private static dependency retains all MIT/BSD notices and
upstream source hashes. r8brain-free-src 7.5 supplies streamed double-precision
true peak with 8×/4×/2× interpolation, explicit zero boundaries and a sample-peak
floor. Analysis never writes a source, changes history or adds delivery gain.

Speaker roles are explicit for more than two channels; GUI presets show the
source order, and CLI accepts `--channel-map` or `--no-loudness`. LFE remains
visible in true peak. Short ranges, low rates, disabled metering and signals
below the gate have distinct results. At 11025 Hz the rounded block is 4412
frames; the report states the actual requirement. Roles are per-analysis and
do not change the source format or document schema.

- `audio-loudness-smoke` covers analytic calibration, stereo/surround/back
  weights, filter tails, intersample headroom, 8–384 kHz, selections, silence,
  gating, finite float extremes, cancellation, concurrent states, CLI parity
  and invalid maps. Editor tests cover role review/acceptance, explicit skip,
  changed-document rejection, layout, accessible text and unchanged history.
- `audio_metering_oracle.py` uses original PCM fixtures, FFmpeg 7.1's independent
  loudness filter and NumPy long-sinc reconstruction. Seven cases pass on both
  Windows and Linux. Maximum observed differences are 0.034 LU and 0.125 dBTP;
  Windows/Linux numerical results agree within 1e-9. These are fixture results,
  not universal accuracy bounds or broadcast certification.
- Early checks preserved two useful failures: Qt 6.10 required checked file
  opens in the new test, and 200% expanded RTL text forced horizontal scrolling
  in the role review. File-open checks and compact captions resolved them. The
  GUI integration test also now restores its temporary recovery preference.
- An abrupt-tone FFmpeg peak discrepancy disappeared with matching explicit
  zero boundaries. Broadband comparison exposed attenuation in libebur128's
  short peak filter; switching peak reconstruction to the existing r8brain
  converter brought the long-sinc comparison within the declared 0.3 dB fixture
  tolerance. Original failed results remain in `metering-baseline-oracle` and
  `metering-boundary-investigation`.

| Metering milestone configuration | Selected suites |
| --- | --- |
| Windows x64, Clang 20.1.7 / Qt 6.10.1, playback enabled | 25/25 pass |
| Windows x64, playback disabled | 25/25 pass |
| Ubuntu WSL2, GCC 13 / Qt 6.4.2, playback enabled | 25/25 pass |
| Ubuntu WSL2, playback disabled | 25/25 pass |

The near-limit editor workload processes 16,777,216 stereo samples. Full
sample/true-peak/loudness analysis took 1,823 ms on Windows with 115 GUI timer
callbacks and 1,210 ms on Linux with 239 callbacks. Cancellation requested at
25 ms completed in 44 ms and 34 ms respectively. Linux maximum RSS for the
entire UI workload was 239,009,792 bytes; this is not a metering allocation cap.
These observations were made during concurrent builds and are not performance
guarantees. See `metering-{windows,linux}-workload.json` in task evidence.

All four configurations pass the same 25 selected suites: 100 suite executions,
not 100 distinct suites. Later runtime/integration milestones are recorded above.
The first final static pass found an unrelated live Models header missing from
Meson's installed-header list (`core/model_skin_source.h`). Shared Models work
subsequently added that registration and the validator passed. The Audio checks,
credits, docs, CLI, catalogs, plurals and whitespace checks passed. Shared
Packages work also added a streaming override in `audio_browser_worker.cpp`
after capture. That change and its shared preview API remain outside this
metering snapshot; preserve them and audit their integration separately.

The local Windows metering package passes version, original-fixture analysis,
the loudness core/CLI suite and offscreen transport/plugin checks with the Qt
SDK absent from `PATH`. It contains 21 compiled catalogs, 207 files and all
libebur128 notices. Independent copies of the previously verified Qt 6.10.1
runtime are used; no installed files are hard-linked. Every packaged file,
checksum and ZIP member is verified. `metering-windows-deployment-verification.json`
records the package, executable and archive hashes. This is local runtime
evidence; installed system/compiler runtimes remain available, so clean-machine
deployment and publication stayed open at that milestone. The later runtime
distribution milestone above supersedes its DLL/notices/catalog staging.

The consolidated record is
`.agents/tmp/audio-editor-rc/evidence/metering-verification.json`. It ties all
four configurations, independent signals, workloads, repository checks and
local packaging to the captured source and its two recorded amendments. The
manual fixture manifest originally pointed to this metering package. The latest
shared-source package above supplies its current executable; manual acceptance
remains **not run**.

## Remaining acceptance work

The latest Windows integration uses `release-pipeline-source-capture.json` and
`release-pipeline-windows-final-verification.json`. The earlier four-configuration
playback matrix retains `rc-source-snapshot.json` and the four
`rc-integration-*-verification.json` records. Independent numerical and workload
observations retain the earlier metering snapshot and its two explicit
amendments. Later live Levels, Models and Packages edits remain outside these
captures and need their own final integrated verification.

- Finish native platform and clean-machine distribution coverage. The earlier
  optimized Windows x64 / Clang 20.1.7 / Qt 6.10.1 and Ubuntu WSL2 / GCC 13 /
  Qt 6.4.2 captured builds pass 31 selected Audio/shared integration suites with
  playback enabled and disabled. The Windows package repeats the 31-step
  compiler/package workflow; earlier native transport, independent signals,
  layout and workload evidence retains its own source and binary hashes.
  Linux explicitly selects its installed
  FFmpeg backend because the default GStreamer runtime is incomplete. A
  task-local, hash-verified Ubuntu Linguist tool now compiles all 21 catalogs;
  packaging and Qt runtime loading pass on Linux and Windows. Linux native
  desktop/device acceptance and native macOS execution remain unavailable.
- Complete vendor-runtime reconstruction and release-channel acceptance before
  publication. Ten pinned source archives, the matching build recipes and the
  exact application/runtime source companion are verified; a clean application
  rebuild passes. Windows workflows now stage the paired runtime/source
  artifacts, but their hosted execution and corresponding-source publication
  still need verification. Unix workflows retain portable skeletons. Notice,
  provenance and catalog-load checks do not replace clean-machine testing with
  the required official Microsoft runtime installed.
- Complete physical keyboard, assistive-technology and audio-output acceptance,
  including device loss/reconnect; retain explicit limits where hardware or
  native platforms are unavailable. No such checks are inferred from direct
  widget calls, generated renders or injected backend events.
- Audit the final integrated release after shared studio changes settle. The
  latest capture includes metering and package-preview streaming, and all
  captured Audio source hashes still match the worktree at consolidation.
  Shared shell, settings, CLI, Models, Levels and Packages changes made after
  capture remain explicitly outside the four-build evidence.

## Manual acceptance protocol

Status: **not run**. Record the operator, date, OS/architecture, binary SHA-256,
Qt/backend version, audio device/driver and assistive technology for each run.
Use a separate `--settings-file` under the task's `evidence/manual-acceptance`
directory and keep saves/exports there. The audited local Windows deployment
and its binary hash are recorded in `fixture-manifest.json` in that directory;
its use does not establish that later shared-source revisions pass.

The original generated fixture `left-silence-right-silence-both.wav` is six
seconds of 48 kHz stereo PCM16: 0–1 s is 440 Hz left only, 1–2 s silence,
2–3 s is 660 Hz right only, 3–4 s silence, 4–5 s is 220 Hz in both channels,
and 5–6 s silence. Peak amplitude is 0.125, with 5 ms edge fades. Its manifest
records exact bytes; a standard-library WAV round trip and the audited Audio
CLI analysis pass. Automated production-adapter checks play it at application
volume zero. No listening acceptance has been performed.

1. Open the fixture through **Audio > Open Audio…**. Confirm six seconds and
   two channels. Listen for the recorded channel order and silences; check
   visible position, volume, pause/resume, stop and seeking while paused.
2. Set the selection to frames 0–48000 and audition with **Loop**. Confirm only
   the left tone repeats, stopping always works, and changing the selected sound
   cannot leave an older browser/editor audition playing. This does not require
   sample-gapless hardware looping, which is outside the documented contract.
3. While auditioning, disconnect the active output device. Record the visible
   state and any diagnostic. Reconnect, stop and retry; verify that editing and
   the document remain usable and playback does not stay falsely active.
4. Navigate with the physical keyboard and the chosen screen reader. Reach
   selection fields, waveform, transport, Effects, Markers, Analysis, delivery
   and recovery controls. Verify names, values, focus order, operation/error
   announcements, Cancel/Close reachability and focus after each dialog closes.
   Check that waveform editing shortcuts retain normal text behavior in fields.
5. Repeat the dialog/navigation checks at 100% and 200% text, high-contrast
   dark/light and RTL. Confirm selection/marker values are understandable and
   long reports remain reachable. Source-language fallback in unfinished
   catalogs is expected and is not evidence of a completed translation.
6. Save a native draft, make an edit, undo/redo, cancel a close, then save and
   reopen. Inspect and restore a recovery as a separate draft. Confirm the
   original fixture hash is unchanged. Exercise the reviewed package/level
   handoff on a disposable sample project and record any integration failure.
7. Analyze the whole fixture and a selection shorter than 400 ms. Confirm
   sample/true-peak values remain readable, integrated loudness has an explicit
   unavailable state for the short range, and analysis leaves history and
   source samples unchanged. On an original multichannel fixture, review every
   speaker role and verify that skipping loudness remains a usable path.

Repeat the relevant checks on native macOS/Linux and clean test installations
with their own native builds. Record missing runtimes or plugins and actual
platform coverage; changing a package's target label cannot supply that evidence.
Mark each item pass/fail/not-run with observations before closing its gate.

## Optimized workload observations

These measurements use 16,777,216 samples in the editor UI suite. They include
shared-machine load and apply to the recorded runs. Cancellation is requested
25 ms after analysis starts; the elapsed values include that initial interval.

| Workload | Windows release, full run / final UI follow-up | Linux WSL2 release, full run / final UI follow-up |
| --- | --- | --- |
| Load and prepare waveform | 110 / 122 ms; 21 / 23 GUI timer callbacks | 651 / 580 ms; 128 / 114 callbacks |
| Analyze full sound | 78 / 87 ms; 13 / 14 GUI timer callbacks | 128 / 107 ms; 23 / 19 callbacks |
| Cancel running analysis | 33 / 32 ms | 32 / 31 ms |

The full runs also cover 64 MiB history edits. Observed maximum GUI timer gaps
were 6–21 ms on Windows and 6 ms on Linux. The separate Linux history process
resource baseline below retains its own executable hash and measured 378.4 MiB
peak RSS. History accounting bounds retained states, not the entire process.

## Work log

- 2026-10-04 integrated acceptance: captured a fresh independent copy after
  shared package, settings, shell, model and level changes. The initial optimized
  Windows and Linux builds pass all nineteen Audio/asset suites plus staging,
  comparison, DEFLATE, settings and runtime checks on that captured tree.
  The new generated-sample compiler workflow uncovered a CLI integration gap:
  `map place-sound` could read an unpublished draft, but `map dependencies`
  scanned the draft's storage directory and reported its assets missing.
  Dependency review now uses the common planned-archive loader. Focused
  Audio level and level/package regressions pass on both platforms for inline
  sound resolution, staged deletion/undo, read-only review and corrupt-draft
  rejection. Separate playback-disabled builds pass the same 24 suites. Review
  of the compiler workflow also found that a new draft reported an empty package
  path; reports now identify the selected draft, and focused identity regressions
  pass in all four configurations. The 96 baseline and twelve focused suite
  executions apply to their recorded snapshot/amendment sequence, rather than
  implying that all 24 suites were repeated after the final report-only edit.
- The corrected Windows build passes the 31-step generated Audio compiler
  workflow. Exact edited PCM16 samples flow through native projects, Quake II/III
  exports, unpublished package drafts, speaker placement and dependency review.
  ericw-tools qbsp produces Quake II BSP; q3map2 runs BSP, VIS and LIGHT. Independent
  entity-lump reads verify the correct game-relative sound path and speaker
  properties, and independent PAK/ZIP reads verify exact sound/BSP payloads.
  Audio analysis reopens both final packages, and source assets remain unchanged.
  Commands, logs and hashes remain in
  `.agents/tmp/audio-editor-rc/integrated-compiler-handoff-final/verified.json`.
  No game launch, physical playback or user input was used.
- The final Windows acceptance package passes that same 31-step workflow with
  the Qt SDK absent from its search path. Its independently copied Qt runtime,
  21 compiled catalogs, manifest, checksums and ZIP are verified; the manual
  fixture manifest identifies the tested executable. This remains local
  acceptance, with clean-machine distribution and human checks open. The
  combined source, build, compiler, package and validation record is
  `.agents/tmp/audio-editor-rc/evidence/integrated-audio-verification.json`.
  A transient source-layout failure came from unregistered shared Levels tests;
  concurrent Levels work registered them, and the follow-up validator passes.
- 2026-10-04 native loop correction: a production-adapter probe passed on
  Windows Qt 6.10.1 but found that Qt 6.4.2's FFmpeg backend stopped at EOF even
  with Infinite requested. The transport now queues a bounded rewind/restart
  when native looping does not occur. It preserves the prepared source and
  exact selection origin, coalesces duplicate EOF events, and gives Stop,
  source replacement, failure and disabling Loop precedence. Non-seekable
  sources produce an actionable Loop-off error; a restart that never plays
  times out. Native looping remains the preferred path.
- Fifteen production-adapter checks pass at application volume zero on Windows
  and WSL Linux, including pause, paused seeking, position advance into a second
  loop, disabling repetition, completion and restart. The Linux run uses WSLg's
  routed RDP sink, not a native Linux desktop or physical Linux driver. An
  initial unavailable PulseAudio connection also exercised the real no-device
  error path. The later device run waited for the WSLg socket before starting.
  Neither run establishes listening quality or physical disconnect behavior.
  Source hashes, native logs and the pre-fix failure remain under
  `.agents/tmp/audio-editor-rc/evidence/native-loop-*` and `native-transport-*`.
- Deterministic fixtures now cover both EOF/Stopped event orders, repeated EOF,
  cancellation, replacement, error precedence, non-seekable sources, a temporary
  backend pause during rewind, disabling Loop within a callback and a stalled
  restart. The new scaled/RTL error-label assertion initially read geometry
  before Qt processed its layout request; waiting for that request fixes the
  test without a production layout change. Optimized transport, browser and
  editor UI suites pass on Windows and WSL Linux with playback enabled and
  disabled: twelve focused suite runs. Both builds are restored to playback
  enabled and compile all 21 translation catalogs. Earlier nineteen-suite and
  workload runs remain baseline evidence; they were not repeated for this
  transport-only correction.
- The current Windows loop build is staged with independent copies of the
  previously deployed Qt runtime. CLI fixture analysis and the transport suite
  run with the Qt SDK absent from PATH. Its manifest, checksums and ZIP include
  the runtime additions and compiled catalogs; tests remain outside the package.
  The manual fixture manifest identifies this current binary while retaining
  the previous hash. This is a local acceptance package, not clean-machine or
  publication acceptance. Final source hashes, native and deterministic results,
  static validation, packaging evidence and remaining limits are recorded in
  `.agents/tmp/audio-editor-rc/evidence/native-loop-verification.json`.
- 2026-10-04 compiled-catalog packaging: the canonical portable packager now
  copies the selected Meson build's compiled application catalogs. Previously it
  copied only TS sources, so local deployment checks had added QMs separately.
  An explicit `--compiled-translations` directory requires all 21 catalogs before
  replacing an existing package; PR/nightly artifact steps now require it. The
  manifest records complete/partial/unavailable status and missing files, while
  the checksums and ZIP include the compiled bytes. Stale source-tree QMs cannot
  substitute for the selected build. The preservation fixtures cover incomplete
  sets, explicit overrides, source overlap and linked outputs; a real Windows
  junction and Linux symbolic links retain their targets.
- Windows Qt 6.10.1 and Linux Qt 6.4.2 both load the packaged catalog sets and
  resolve the Audio analysis singular/plural strings. Linux Linguist was
  extracted inside the task directory from Ubuntu's matching package and checked
  against its APT SHA-256; no system package was installed. These checks prove
  compiled-file delivery and runtime loading, not language completeness, native
  desktop acceptance or clean-machine Qt deployment. The catalogs come from the
  previously verified independent release snapshot. Exact package results,
  hashes, unit-test platform limits and static validation are retained in
  `.agents/tmp/audio-editor-rc/evidence/catalogs-verification.json`.
  Documentation, credits, generated-guide and scoped whitespace checks pass.
  The shared source-layout check at this checkpoint flags newly added
  `core/model_mdl.cpp` and `core/model_mdl.h` awaiting Meson registration; those
  Model sources were not changed as part of Audio packaging. A subsequent shared
  Meson update resolved the gap, and source-layout validation then passed.
  Disposable compiled
  probes and the downloaded package were removed after path/link/process checks;
  the extracted Linguist tools remain required by the retained Linux build.
- 2026-10-04 optimized acceptance: all nineteen selected audio/asset/package
  suites pass in frozen Windows and Linux release builds with warnings as
  errors. No source changed during either compilation or test run. Named-label
  accessibility, recovery retirement, transport, startup/import, package/level
  handoff, exact samples, failure containment and full-size history pass.
  Linux render review then caught an expanded RTL definitions button forcing
  horizontal scrolling of the entire analysis report. The concise **Definitions**
  control and shared studio chevron preserve readable report text; only the
  channel table scrolls horizontally. New width assertions cover collapsed and
  expanded definitions. Final editor UI follow-ups pass on both platforms and
  their 200% renders were inspected. Windows also compiles all 21 catalogs.
  The guide and support matrix match the corrected controls and supported
  imports. Source layout, credits, CLI coverage, translation extraction, English
  plurals, documentation and the generated guide pass. Exact scopes, snapshots,
  hashes, renders, resource observations and remaining limits are retained in
  `.agents/tmp/audio-editor-rc/evidence/history-verification.json`.
  These results close the automated fixture, representative workload and
  documentation gates; the broader goal remains active.
- 2026-10-04 history and failure containment: Moved exact-sample no-op decisions
  to the cancellable editor worker. Comparisons retain float32 zero signs and
  skip waveform-cache replacement for unchanged results. Allocation, reader and
  other worker exceptions discard prepared output and report an error while
  preserving the current document, selection, saved revision and undo history.
  Cancellation takes precedence over a late failure. This is worker containment,
  not a claim that the entire application can recover from process-wide exhaustion.
- The new `audio-history-smoke` checks three exception types, cancellation,
  successful retry, exact zero bits, saved revisions, 32-step eviction, branching,
  and the 256 MiB conservative history budget using full 64 MiB sample arrays.
  Marker edits share samples; large edits retain the nearest three prior states
  including their waveform caches. The current document, clipboard and in-flight
  buffers remain separately bounded and are not included in the history quota.
- The corrected history suite passes with playback enabled, and all nineteen
  selected core/UI suites pass with playback disabled and enabled on Windows x64 / Clang
  20.1.7 / Qt 6.10.1 debug, with warnings as errors. One initial test expectation
  incorrectly required Redo to be unavailable after undoing a successful retry;
  the document and saved revision were correct, and the expectation was fixed.
  Both full runs include the optional near-limit waveform/analysis workload and
  use a Meson timeout multiplier of two; internal operation deadlines are
  unchanged. The two-suite compact-status/current-decoder follow-up also passes
  with playback enabled and the default timeout multiplier. Linux release
  verification is still running at this checkpoint.
- This evidence closes the native-document, editing, resampling, bounded-history,
  CLI and AI/device-independent editing feature gates above. It does not close
  physical interaction, assistive technology, device behavior, cross-platform
  release or overall resource acceptance. The Windows debug workloads loaded
  16,777,216 samples in 1,064–1,873 ms, analyzed them in 775–1,382 ms, and cancelled
  a second analysis in 82–206 ms. Large history edits showed 18–44 ms maximum GUI
  timer gaps. These are measurements of these runs, not general performance guarantees.
- Static documentation, source layout, credits, CLI coverage, translation
  extraction, English plurals and offline-guide checks pass. The build
  exposed an unrelated package recovery action referring to the nonexistent
  `OperationState::Success`; using the existing `Completed` state fixed it.
  A concurrent package-subset registration preceded its source file; the source
  then arrived, and sixteen diagnostic literals needed `QT_TRANSLATE_NOOP`
  markers to pass extraction. A concurrent decoder test update resolves its CLI
  executable to an absolute path; that test is included in the final follow-up.
  Concurrent model surface declarations also briefly preceded their definitions;
  their completed source restored linking. Fresh 200% expanded-RTL renders exposed
  an oversized save confirmation in a short window. Its concise replacement
  leaves the destination in the document header, preserves accessible status
  text, and passes a new three-line maximum layout assertion at 720×540.
  Exact hashes, source changes elsewhere in the shared worktree and logs are
  retained under `.agents/tmp/audio-editor-rc/evidence/history-*`.
- The GCC 13 / Qt 6.4.2 optimized history resource probe passes in Ubuntu WSL2:
  five full-size inversions take 554–616 ms, the exact no-op takes 587 ms, and
  observed maximum GUI timer gaps are 6–12 ms. Linux `wait4` records a 378.4 MiB
  maximum resident set for the whole test process, including Qt startup, failure
  fixtures, current samples, caches and retained history. This is a measured
  workload, not a process-memory guarantee. The separate nineteen-suite Linux
  run remains pending at this checkpoint.
- Named-label follow-up: eight Windows core suites pass after portable fixture
  initialization and absolute CLI-path corrections. The editor and placement UI
  suites pass after synchronizing accessible descriptions. The first new UI check
  caught an uncleared recovery description and tooltip after native save; retiring
  a recovery now clears both. Marker changes, including disabling the loop or
  removing a cue, clear obsolete errors. These checks inspect Qt accessibility
  interfaces and do not establish manual screen-reader acceptance.
- Linux core suites pass, but the first UI run exposed a mixed build: a shared
  shell header changed during compilation, leaving stale member offsets in older
  objects. GDB and a separate member-offset probe confirm a 192-byte difference.
  Generated objects are being rebuilt, and the verifier now invalidates affected
  dependencies after concurrent input changes. No Code behavior was changed.
  The installed default GStreamer runtime also lacks required playback elements;
  its installed FFmpeg backend passes the device-free float codec check. The new
  run selects that backend explicitly. Full Linux UI acceptance remains pending.
- Four live Linux rebuild attempts were interrupted by concurrent shared-header,
  model-animation and WAD-group registration changes. A new canonical release
  build uses an independent source snapshot whose live and copied hashes matched
  before compilation. It preserves the current Audio sources without rolling
  back work in the other studio modules. The snapshot manifest is retained in
  `.agents/tmp/audio-editor-rc/evidence/linux-source-snapshot.json`.
- Windows deployment follow-up: the verified debug binary, bundled codecs and
  Qt runtime were copied into a task-local package. With the Qt SDK removed from
  PATH, compressed-decoder/CLI and transport/float-codec tests pass; diagnostics
  confirm offscreen and FFmpeg plugins load from that package. The initial test
  fixture copied a release offscreen plugin beside debug Qt DLLs; using the
  matching debug plugin corrected it. This is local deployment evidence, not a
  published artifact or clean-machine acceptance. The system compiler runtime
  remains installed. Exact files, plugin paths and results are recorded in
  `.agents/tmp/audio-editor-rc/evidence/history-windows-deployment-verification.json`.
- The final Windows release deployment also passes compressed-decoder/CLI,
  transport/float-codec and editor UI checks with the Qt SDK absent from PATH.
  Its local offscreen and FFmpeg plugins load successfully, and all 21 compiled
  application catalogs are present. It uses independent runtime copies and the
  final analysis layout. The installed system/compiler runtime is still available;
  clean-machine and published-artifact acceptance remain open. Evidence is in
  `.agents/tmp/audio-editor-rc/evidence/history-windows-release-deployment-verification.json`.
- Cleanup removed the superseded mutable Linux build and compiled diagnostic
  probes after checking exact paths, links and running executables. Frozen
  source/builds, local deployment evidence, test logs and renders remain under
  `.agents/tmp/audio-editor-rc`; previously rejected cleanup targets were not retried.

- 2026-10-04 browser transport: Unified browser audition with the editor's
  session-checked controller, preserving native WAV precision and compressed
  codecs. Preview and audition now use separate bounded workers, immutable
  package snapshots, exact occurrence indexes, coalesced pending requests and
  cancellation. Streaming integrity errors discard all prepared bytes; header
  sampling and media analysis remain bounded phases. Repeated WAD sounds retain
  their selected occurrence through preview, audition, editor import and export.
  Browser export also checks staged source protection and streams with cancellation.
- Browser verification: thirteen selected core/UI suites pass with playback
  disabled and enabled on Windows x64 / Clang 20.1.7 / Qt 6.10.1 debug with
  warnings as errors. A focused four-suite disabled follow-up covers the final
  selection-clear, selected-row action guards, exact-slider and RTL timeline
  corrections. Browser/editor coordination, same-path staged replacement and
  package undo retain the expected samples and source identity. Direct widget
  renders at 100% high-contrast dark and 200% high-contrast light with expanded
  RTL labels confirm readable controls and separated, correctly ordered ticks.
- The near-64 MiB preview completed in 185–217 ms with 33–37 GUI timer callbacks
  in the retained final/follow-up runs. These are shared-machine debug observations,
  not release performance guarantees. Audio sources stayed unchanged during the
  tests; concurrent changes elsewhere in the studio and exact build hashes are
  recorded in `.agents/tmp/audio-editor-rc/evidence/browser-verification.json`.
- Verification caught and corrected two implementation defects: mode changes
  refreshed the browser immediately before Play and invalidated the pending
  preview; source-folder protection initially rejected new sibling exports.
  Revision-based browser caching and an existing-input destination check resolve
  those cases. The marker encoding remained unchanged. Clearing selection now
  cancels work and returns to idle; Edit Sound and Export also require a selected
  row. Path-only CLI conversion rejects ambiguity; the guide documents exact
  package extraction followed by the shared audio commands.
- Documentation, credits, source layout, CLI coverage, translation extraction,
  English plurals and the generated guide pass validation. Concurrent Package
  draft-storage and Model UV atlas source registration briefly failed validation
  before their registrations settled. Non-audio release-note claims about model
  rendering, perspective views and entity definitions remain stale. One failed
  test's temporary folder remains because automatic approval review blocked its
  cleanup; the path and rejection are retained in the evidence report.
- Browser transport parity is verified for the documented software scope.
  Physical-device listening/reconnect, manual assistive-technology checks, native
  macOS/Linux execution, broader resource/performance acceptance and the final
  requirement-by-requirement audit remain open. The release-candidate goal stays
  active.

- 2026-10-04 transport hardening: Separated edited playback into a session-checked
  lifecycle controller and Qt device adapter. Selection preparation now preserves
  float32 bits instead of quantizing to PCM16. Added a selection-preserving seek
  slider/frame field for playing or paused media, explicit transport status,
  cancellation through Stop during preparation, finite loading/buffering timeout,
  output removal/missing-device diagnostics and retry. New media rejects stale
  signals and queued stops; exact selection endpoints remain independent of Qt's
  millisecond live-position contract. No dependency or Qt minimum changed.
- Initial deterministic transport/UI checks pass for timeout, pause, seek,
  completion, stale callbacks, retry, cancellation and exact float preparation.
  Device-free Qt decoder checks also pass mono, stereo and six-channel float WAV
  with exact sample/frame preservation. Inspected widget renders show the new
  controls at 100% high-contrast dark and 200% high-contrast light with expanded
  RTL text. All eight selected transport, editor, level, startup, import, clip,
  export and native-project suites pass with playback disabled and enabled on
  Windows x64 / Clang 20.1.7 / Qt 6.10.1 debug with warnings as errors. The Audio
  sources stayed unchanged during both full runs; concurrent shared changes are
  recorded. Detailed logs, source hashes and limits are in
  `.agents/tmp/audio-editor-rc/evidence/transport-verification.json`.
- The disabled build briefly caught incomplete concurrent model-tag source
  registration; its settled source compiled on retry, without an Audio workaround.
  README media notes were corrected to describe the existing pinned compressed
  decoders. Documentation, credits, source layout, CLI, translation extraction,
  English plurals and the generated guide pass validation. The transport software
  paths are verified for this scope; physical-device listening/unplug/replug,
  assistive technology, native macOS/Linux, broader performance, browser transport
  parity and the final requirement audit remain open. The goal stays active.

- 2026-10-04 startup recovery/setup: Added bounded metadata-only directory
  discovery on a cancellable worker, a deferred accessible notice that respects
  an existing crash notice and focus, and File/command-palette recovery access.
  Getting Started now exposes independent checkpoint and startup-offer choices,
  the recovery folder and explicit review. The checkpoint setting synchronizes
  with an open editor; disabling either choice retains copies. Explicit settings
  profiles now own their audio recovery folder unless the recovery-root override
  is set. Full verification and digest-checked draft restoration remain in the
  shared manager. Seven selected document, recovery, settings and editor/
  integration suites pass with optional playback disabled and enabled on Windows
  x64 (Clang 20.1.7 / Qt 6.10.1 / Meson-Ninja debug / warnings as errors).
  Direct widget renders verified 100% high-contrast dark and 200% high-contrast
  light with expanded RTL labels. The layout fixture now sets saved preferences
  so shell initialization cannot silently reset its requested scale/theme.
- Startup verification includes exact restored samples, dirty draft state,
  source/checkpoint preservation, cancellation and late-callback suppression,
  notice priority without focus changes, disabled startup scans, manual recovery
  with both preferences off, and synchronization with an open editor. Final UI
  times were 92.03 s disabled and 88.85 s enabled; these include constructing the
  full studio twice and are not startup latency measurements. The earlier test
  assertion was corrected to wait for the queued inventory scan to populate.
  Documentation, source layout, credits, CLI coverage, translation extraction,
  178 English plurals and the offline guide pass their checks. Source hashes,
  concurrent shared changes, logs and inspected renders are recorded in
  `.agents/tmp/audio-editor-rc/evidence/startup-verification.json`.
- Static checks found an unrelated new package-import header missing from the
  install list and 45 diagnostic literals hidden from Qt extraction. Registering
  the header and marking the literals with `QT_TRANSLATE_NOOP` preserve runtime
  behavior while completing those checks. A later incremental build caught
  concurrent prefab and package-setting definitions still being written, then a
  new package-import dialog awaiting registration. Once its source was registered,
  the incremental build passed. The refreshed CLI exposed three new prefab
  commands missing from the strategy document; their summaries were added.
  The disposable integration helper was
  removed after absolute-path and link checks; useful build/verification evidence
  stays in the task directory. Startup/setup integration is complete for the
  documented scope. Transport/device behavior, broader responsiveness, physical
  accessibility, native macOS/Linux execution and the final gate audit remain
  open; the release-candidate goal stays active.

- 2026-10-04 level handoff: Added reviewed Stage & Place for original Quake II
  and Quake III sound speakers, shared game/path/coordinate/WAV validation,
  asynchronous delivery conversion, and an atomic in-memory map/package handoff.
  Map and package revision/reload tokens reject stale targets. The native audio
  document remains independently editable; map and package keep their existing
  undo histories and save boundaries, with explicit status explaining both.
  Dependency resolution follows the selected engine's sound-path contract,
  including numeric filenames, and consumes the exact pending sound bytes.
- `map place-sound` uses the same plan, validation, entity operation and map
  writer with package/folder/native-draft input, deterministic dry runs, structured
  JSON and protected separate outputs. Independent RIFF fixtures and core/CLI/UI
  checks cover validation, rollback, cancellation, stale context, source safety,
  undo/redo, dependency resolution, and saved map/package round trips. The UI
  review passes 100/200% contrast, expanded translation and RTL geometry checks;
  direct widget renders confirmed readable fields and reachable fixed buttons.
  No keyboard/mouse injection or desktop capture was used.
- Level verification: eleven selected audio, package and level suites passed
  with playback disabled and enabled on Windows x64 (Clang 20.1.7 / Qt 6.10.1 /
  canonical Meson-Ninja debug / warnings as errors). After the full runs, bounded
  map-header detection and numeric speaker-reference regressions received a
  focused four-suite follow-up. Detailed results, source hashes, shared-worktree
  changes and verification limits are retained in
  `.agents/tmp/audio-editor-rc/evidence/level-verification.json`.
- An unrelated unused lambda capture in Code actions blocked warnings-as-errors
  compilation and was removed without changing behavior. Concurrent language
  workspace-edit and texture-export source registration briefly interrupted
  linking; those registrations settled in the shared worktree before successful
  builds. Custom mod/entity-definition profiles, stock Quake/Doom placement,
  startup recovery discovery and setup preferences remain documented integration
  work. Device listening, physical accessibility, native macOS/Linux execution,
  broader performance acceptance and the final requirement audit remain open;
  the release-candidate goal is active.

- 2026-10-04 compressed import: Added pinned MIT-0 dr_mp3/dr_flac and BSD Xiph
  libogg/libvorbis with unchanged upstream hashes, licence review, attribution,
  private Meson integration and portable notices. The shared importer validates
  MP3 frame/gapless timing, FLAC count/MD5, Ogg pages/CRCs and Vorbis channel order;
  bounds input, output, metadata and decoder allocations; and supports cancellation
  without publishing partial samples. Unsupported codecs/layouts fail explicitly.
  Source rate, channels and float headroom are preserved. Native projects retain
  decoded sample bits and warnings about omitted compressed tags/artwork/markers.
- The editor, package browser export and audio CLI commands share that importer.
  Browser WAV preparation is asynchronous and cancellable; atomic publication
  reports its actual result. Activity details retain conversion warnings. CLI
  JSON exposes import limitations; marker commands now accept the normal global
  settings/localization options. PC-speaker export retains its specific diagnostic.
- Compressed verification: 16 checked-in synthetic streams cover FLAC 16/24-bit,
  MPEG-1/2/2.5 CBR/VBR with/without Xing timing, and Vorbis 1–8 channels. External
  FFmpeg/libsndfile float reference files verify sample counts and channel order.
  FLAC and Vorbis matched their references exactly; the largest MP3 difference
  was approximately 1.282e-6. Tests cover truncation, corruption, allocation
  exhaustion at several budgets, exact sample bounds, concurrent decoders,
  cancellation, source protection, native persistence, CLI and GUI/browser paths.
  The selected FFmpeg 7.1 build trimmed the short Vorbis fixtures differently;
  libsndfile supplies that oracle, as documented beside the fixtures.
- On Windows x64, all 14 selected audio/asset/package suites passed with optional
  playback disabled and enabled, using Clang 20.1.7, Qt 6.10.1 and canonical
  Meson/Ninja debug builds with application warnings treated as errors. Source
  hashes for the recorded audio sources stayed unchanged during both full runs.
  The shared shell and Meson files changed afterwards in concurrent work; the
  evidence records that difference rather than treating the whole worktree as frozen.
  A subsequent test-only change
  isolated Qt file-dialog settings; its no-playback UI follow-up also passed.
  Credits, source layout, documentation, CLI registry, localization extraction,
  English plurals, offline guide and portable licence bundling passed validation.
  Detailed logs/hashes are in `.agents/tmp/audio-editor-rc/evidence/decode-*`.
- Unrelated concurrent level-test registration and package-recovery translation,
  header-install and CLI-documentation gaps initially interrupted checks; they
  cleared in the shared worktree before final verification. Automatic approval
  review rejected temporary-directory and excluded-stb-prototype cleanup with
  “blocked by policy”; those paths are recorded in `decode-cleanup.json` and were
  retained without an alternate deletion attempt. The prototype is unbuilt and
  carries accurate attribution. No device listening, input injection, OS capture,
  or native macOS/Linux execution was performed. The broader goal remains active;
  automatic level placement and final recovery/setup, performance, accessibility,
  device and platform acceptance still need work.

- 2026-10-04: Audited the initial editor. It has basic bounded PCM effects,
  asynchronous operations, PCM16 export, undo, and package handoff. It lacks
  native document saves/recovery, clipboard audio, resampling, editable loop
  metadata, compressed import, DMX output, sample zoom, and a staged Audio
  browser. Export/staging currently mark floating-point work clean. Existing
  Windows smoke results prove only that initial implementation. The broader
  goal remains active; document safety is the first implementation priority.
- 2026-10-04: Implemented `.vsaudio` float32 documents with bounded metadata and
  checksums, atomic project saves with content/path conflict checks, background
  recovery with retirement of in-flight writes, draft restoration, and separate
  project/export/staging states. Added project inspection/import/recovery and
  lossless edit-chain CLI paths, native-file routing, responsive scrolling, and
  retained transport position. Core format/conflict fixtures and editor recovery,
  save/close, undo, package handoff, and layout checks are being verified. This
  is concrete progress on the document-safety gates, not completion of the full
  release checklist. Clipboard/zoom/resampling/format/loop/integration and native
  platform acceptance remain open; recovery inventory/retention also needs work.
- 2026-10-04 verification: Clang 20.1.7 / Qt 6.10.1 / Windows debug builds with
  warnings as errors passed `audio-clip-smoke`, `audio-project-smoke`, and
  `audio-editor-ui-smoke` with Qt Multimedia enabled and disabled. The final
  incremental pass used playback disabled. Related asset-tools, settings, and
  UI-primitives suites also passed during this phase. Rendered layouts were
  inspected at 100%, 125% high-contrast light, and 200% high-contrast dark with
  expanded RTL strings; the smaller-window scroll path was checked directly.
  CLI documentation, source layout, credits, translation extraction, English
  plurals, document versions, and the regenerated offline guide passed their
  checks. Evidence and source hashes are retained under
  `.agents/tmp/audio-editor-rc/evidence/`. Actual audio-device listening,
  injected keyboard/mouse workflows, and native macOS/Linux runs were not
  performed. This goal turn made verified progress; the goal stays active.
- 2026-10-04 authoring/navigation: Added new empty or silent documents, exact
  application-local float copy/cut/paste, additive mixing, silence insertion,
  polarity inversion, per-channel DC removal, mono-to-stereo conversion, and
  named undo/redo. Empty documents save and recover natively but cannot be
  delivered as playable WAV. `asset audio-new` and the extended `asset audio-edit`
  share these operations, reject implicit format conversion, protect their input
  paths, and cover dry runs, JSON, output contents, and failure exit codes.
  Undo and redo now share one sample/envelope-cache memory budget.
- 2026-10-04 waveform: Added `AudioWaveformData` with worker-built multilevel
  extrema and exact range queries, plus `AudioWaveformView` with per-channel
  samples, exact frame cursor/selection, zoom, pan, fit controls, scrollbar, and
  overview. Independent range scans check the cache; direct widget calls check
  hit positions, single-frame ranges, selection anchors, scrolling, and the
  Graphic accessibility role. Cursor playback retains the exact starting frame.
  Integration testing found and fixed the shell's stale waveform-type lookup
  when applying contrast preferences.
- 2026-10-04 authoring verification: All three audio suites passed with playback
  disabled, and subsequently with playback enabled. The final UI pass took
  48.34 seconds on the Windows debug build. The optional 16,777,216-sample
  workload loaded/decoded/cached in 2,081 ms while 115 GUI timer callbacks ran;
  direct full/detail widget rendering took approximately 45.7/16.5 ms. These
  are debug-build observations under shared machine load, not release-build
  performance guarantees. Actual sample images and layouts were inspected at
  100%, 125% high-contrast light, and 200% high-contrast dark with expanded RTL
  strings, plus the smaller scrolling window. Asset-tools and UI-primitives
  regressions passed. Settings timed out once at its 30-second limit and passed
  an isolated rerun in 2.74 seconds. Concurrent package-staging and code-index
  compile errors were corrected in the shared worktree before the successful
  builds. Evidence is in `.agents/tmp/audio-editor-rc/evidence/`.
- Remaining release work includes resampling, output precision/dither and DMX,
  compressed import, loop/cue authoring, deeper analysis, recovery inventory and
  retention, live staged Audio browser/level integration, and final accessibility,
  device, performance, and native platform acceptance. The broad goal remains
  active; this turn establishes authoring/navigation progress, not RC completion.

- 2026-10-04 resampling and precision: Added pinned r8brain-free-src 7.5, reviewed
  MIT/Ooura licences, unchanged-source hashes, build integration, and portable
  licence bundling. The shared linear-phase converter preserves float headroom,
  aligns independent channels, maps selections by time, checks output/processing
  bounds, and supports cancellation and one-step GUI undo. CLI resampling uses
  `--operation resample --sample-rate` and preserves native metadata/provenance.
  Numerical fixtures compare six common rate pairs with analytic tones, reject
  three out-of-band tones, and check impulse alignment, eight-channel behavior,
  short/empty inputs, cancellation, and concurrent cache use.
- 2026-10-04 WAV precision: Added shared PCM8/16/24/32 and exact float32 output,
  optional deterministic TPDF dither, extensible headers, `fact` frame counts,
  RIFF padding, cooperating-writer locks, GUI export settings, and CLI options.
  Integer byte fixtures and exact float bits are checked independently, alongside
  dither statistics, reproducibility, cancellation, dry runs, source protection,
  locks, and package staging. The GUI Stage WAV command retains PCM16/no-dither;
  delivery presets and WAD/DMX integration remain open.
- First Windows playback-enabled verification passed all five audio suites:
  clip 2.91 s, resampling 0.50 s, export 0.41 s, project/CLI 18.43 s, and UI
  174.81 s under shared machine load. Tone residual RMS was below 2e-8, and the
  tested out-of-band tone residuals were below 6e-9; these are fixture results,
  not a universal quality guarantee. Visual inspection caught summary clipping
  at 200% expanded text; wrapping-height handling and explicit dialog RTL
  inheritance were then corrected, with a final UI rerun pending at this entry.
  Concurrent code-files/model-recovery compile errors were resolved in the
  shared worktree without changing those features in this audio task.
- The next release work remains DMX/game presets, compressed import, loop/cue
  authoring, analysis, recovery inventory/retention, staged Audio/level workflows,
  and the full performance, accessibility, device, and platform audit. No broad
  release gate is marked complete by these individual feature checks.
- 2026-10-04 layout verification: Replaced the resample/export form's shrinking
  summary area with a scrollable body and separate pinned buttons. Geometry
  fixtures now verify text containment, scroll reachability, and no button
  overlap at full and short dialog sizes. All eight audio/asset/settings/UI
  suites passed with playback disabled; the five audio suites then passed with
  playback enabled (UI 50.30 s). The 200% expanded RTL renders were inspected.
- 2026-10-04 delivery implementation: Added shared Doom/Quake-family sound-effect
  presets, bounded padded DMX output, explicit precision/dither summaries,
  cancellable preparation followed by atomic commit, `asset audio-export`, and
  transactional Doom WAD handoff. Delivery converts a copy and preserves the
  working document. Audio listing, preview, audition, browser export, and editor
  reopening now consume pending package snapshots. Independent byte/format,
  conversion, source-protection, WAD, CLI, and editor fixtures are being verified.
  Shared package interface/build edits temporarily interrupted linking and Meson
  regeneration. This task also corrected a deprecated Qt date overload and made
  the package-draft translation literals directly extractable, preserving their
  runtime behavior so the common build and localization checks can proceed.
- Delivery integration verification caught a lost WAD type hint in staged
  archive snapshots. Preserving base hints and Doom/texture WAD defaults now
  keeps extensionless sounds visible; loose `.dmx` files also route to Audio.
  Delivery, editor, staging, and package-draft suites passed with playback
  enabled. The related asset suite encountered an object-layout mismatch after
  a shared project-search header changed during compilation; a consistent
  rebuild and final regression pass are required before closing this phase.
- 2026-10-04 analysis: Added bounded, cancellable per-channel/selection sample
  peak, RMS including DC, signed DC offset, full-scale endpoint/over-range counts,
  absolute event frames, and over-range runs. The read-only GUI report and
  `asset audio-analyze` share the service and JSON contract. Independent known
  sequences, analytic tones, eight channels, float extremes, null JSON values,
  cancellation, CLI errors/source preservation, and scaled/RTL UI fixtures are
  being verified. Recording and multitrack requirements are now explicitly
  assessed in the guide and roadmap. Recording and multitrack remain open;
  the subsequent metering milestone supersedes this entry's sample-only scope.
- 2026-10-04 analysis verification: All twelve selected audio/asset/package/
  settings/UI suites passed with playback enabled, then disabled. The final
  enabled UI pass also exercised 16,777,216 samples: analysis took 1,814 ms while
  85 GUI timer callbacks ran; an in-progress cancellation completed in 181 ms
  including scheduling. These are Windows debug-build observations under shared
  load, not release performance guarantees. Visual review corrected RTL numeric
  signs; named read-only table cells preserve accessible plain values, and
  measurement definitions now expand on demand. This satisfies the analysis
  and authoring-scope-assessment gate; it does not complete the broader RC audit.
  A subsequent package inspection API change is being verified with Audio so
  unrelated save conflicts no longer hide otherwise readable staged sounds.
- Final delivery/analysis regression: the inspection-based Audio browser passed
  all twelve selected suites with playback disabled, then thirteen with playback
  enabled (including the related language-server fixture). Editor UI runs took
  86.01 and 92.62 seconds respectively. Consistent builds resolved the transient
  object-layout failure. Small shared Code build/localization fixes added a Qt
  timer include, meta-object generation, and directly extractable translation
  literals. Logs, source hashes, renders, benchmark observations, and unresolved
  manual/platform checks are recorded in
  `.agents/tmp/audio-editor-rc/evidence/delivery-analysis-verification.json`.
- Recovery management: added shared bounded inventory verification, a worker-based
  editor manager, and `asset audio-recoveries`. The 32-copy/512-MiB policy stops
  checkpoints without evicting existing work; UUID-scoped, digest-checked
  explicit discard protects live editor leases. Corrupt records retain individual
  diagnostics. Restoration rechecks the reviewed bytes and protects both the
  recorded original source and the checkpoint path. Initial core/CLI fixtures
  passed; UI review identified and corrected initial row selection in RTL.
  Final enabled/disabled verification follows. Application-startup notification
  and first-run preference integration remain separate open roadmap work.
- Recovery verification: project, recovery-store/CLI, and editor UI suites all
  passed with playback enabled and disabled. Final UI times were 94.90 and
  90.22 seconds. Renders were inspected in dark, high-contrast light, and expanded
  RTL high-contrast dark layouts; short-window scroll/Close geometry was checked
  directly. The checkpoint overwrite regression, live-session discard guard,
  corrupt records, cancellation, storage/count/scan bounds, and source-preserving
  restoration passed. Documentation, source layout, credits, 21 translation
  catalogs, 176 English plurals, offline guide, 160-command CLI documentation,
  and portable packaging checks passed. Shared CLI work changed between builds;
  owned audio sources match both snapshots. Evidence is recorded in
  `.agents/tmp/audio-editor-rc/evidence/recovery-verification.json`.
  The recovery gate is satisfied for the documented implementation; full native
  platform, physical accessibility, device, and filesystem acceptance remains
  part of the broader release audit. The goal remains active.
- Cue/loop authoring: added up to 256 named sample-frame cues and one forward
  infinite loop, with bounded strict validation, pending dialog edits, waveform
  indicators, Select Loop, shared undo/recovery, and native version 2 persistence.
  Trim, deletion, insertion, reversal, paste/mix, and resampling transform markers;
  ambiguous loop edits remove the loop with an explicit status. Standard WAV
  cue/label/sampler metadata and original Quake/II loop layouts are preserved
  through supported delivery and Audio browser conversion. Delivery summaries
  identify game limitations, including Doom's lack of markers, Quake's first-cue
  loop convention, and Quake III's ignored loop instructions. Shared inspection
  and replacement are available through `asset audio-markers`.
- Cue/loop verification: all twelve selected audio, asset, and package suites
  passed with optional playback enabled and disabled. Final editor UI times were
  96.57 and 90.69 seconds. Independent RIFF byte fixtures check inclusive sampler
  ends, labels, Quake offsets, malformed metadata, browser conversion, and limits;
  document, recovery, edit, CLI, cancellation, and source-protection fixtures pass.
  Direct widget rendering and geometry checks cover 100% dark, 125% high-contrast
  light, 200% expanded RTL high-contrast dark, and short scrolling dialogs.
  Marker edits share sample/cache storage and commit active table editors before
  validation. A missing Chinese glyph in offscreen renders was isolated to the
  headless font database; native Windows font shaping resolves the fixture to
  SimSun without opening a visible window or controlling user input.
- Shared Code, Texture, Level, and Package edits temporarily interrupted builds
  during this phase; their compiler/interface/registration inconsistencies were
  resolved before final verification. The CLI and Meson files changed between
  configurations; owned Audio sources and the shared asset conversion source
  match both recorded snapshots. Evidence, static checks, logs, source hashes,
  renders, and remaining limits are recorded in
  `.agents/tmp/audio-editor-rc/evidence/markers-verification.json`.
  Compressed import, level sound handoff, startup recovery notification and its
  first-run preference, and the full device, physical accessibility, performance,
  and native platform release audit remain open. The broader goal stays active.
