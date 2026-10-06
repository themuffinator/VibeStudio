# VibeStudio 0.1.0-rc1

This document records the foundation cut. Its included features and known gaps
are a historical snapshot, not the current editor acceptance report. The active
modeller release requirements and evidence are tracked in
[Modeller Release Candidate Gate](MODELLER_RELEASE.md), and the current texture
acceptance is tracked in the [texture editor audit](plans/texture-editor-release-candidate.md).
Audio requirements, optimized regression results and remaining platform/manual
acceptance are tracked in the [audio editor audit](plans/audio-editor-release-candidate.md).

This is the first MVP release-candidate cut for the foundation scaffold. It is
not a production-ready editor suite; it is a validated baseline for the
project, package, compiler, settings, sample, packaging, credits, and CLI
surfaces that later editor work can build on.

## Included

- Qt6 Widgets shell built around a mode rail and a stacked work surface
  (Workspace, Levels, Models, Textures, Audio, Packages, Code, Shaders, Build,
  Settings), with a menu bar, toolbar, status chips, drag-and-drop, and
  confirmation prompts for destructive actions.
- Command registry that is the single source for the menu bar, toolbar,
  keyboard shortcuts, and a type-to-filter command palette, with shortcut
  conflict detection reported by the shell self-test.
- Workspace dashboard, setup panel, preferences, activity center, inspector
  details, package browser with tree/list views, workspace
  problems/search/changes panels, Steam/GOG installation candidate
  detection/import, safe package extraction, and About/Credits/license surface.
- Painted charts backed by real data: package composition by entry type and
  size, compiler and build pipelines drawn as source, stages, and artifacts with
  per-stage state, and a recent-activity timeline with duration bars. Every
  chart carries a non-color cue and an accessible text summary.
- Interactive 2D map viewport: painted vertices, linedefs, traced sector
  outlines, and things for Doom-family maps; orthographic brush bounds and
  tessellated Quake III patches on top, front, and side projections for
  Quake-family maps. Selection, grid, zoom, and pan are shared with the
  property inspector.
- Asset preview widgets: a zoomable image view with checkerboard alpha,
  nearest-neighbour magnification, mip and frame selection; a 16x16 palette
  swatch grid with index/RGB readout; and a min/max waveform envelope painted
  from decoded PCM peaks.
- Dependency-free DEFLATE codec implemented from RFC 1951/1950, with CRC-32 and
  Adler-32. ZIP/PK3 reading handles stored and deflated entries and ZIP64;
  ZIP/PK3 writing selects stored, fixed-Huffman or dynamic-Huffman blocks.
- Shared package integrity validation streams complete payloads with bounded
  buffers, CRC/size checks, SHA-256 reports and within-file cancellation. The
  GUI and CLI share positional WAD reads, archive comparison and staged review.
- idTech image decoders for Doom patches, flats, PLAYPAL and COLORMAP, Quake
  `.lmp`, WAD2/WAD3 miptex, Quake II `.wal`, PCX, Targa, and Quake `.spr`, with
  the palette resolved out of the open package and a generated, license-clean
  fallback, plus palette quantization for conversion.
- Map parsing with a real tokenizer: Doom lumps and Hexen strides, Quake-family
  `.map` text including Valve 220, `brushDef`/`brushDef3`, and
  `patchDef2`/`patchDef3`, UDMF detection, undo/redo, and save fidelity.
  Geometry is solved by half-space intersection so brush bounds, sector
  outlines, and patch tessellation match the statistics and the viewport.
- Headless, deterministic SVG map rendering usable from the CLI, generated
  documentation, and CI without a display.
- Read-only BSP inspection for Quake, Quake II, and Quake III families: lumps,
  entities, and texture references, plus `.pts`/`.lin` leak point files and
  `.prt` portal files.
- Chained build pipelines (qbsp to vis to light, q3map2 stages, Doom node
  builders) and game launch plans, sharing the compiler runner so logs,
  diagnostics, hashes, and manifests are identical to a single-profile run.
- Compiler runs with streamed output, cancellation, opportunistic
  warning/error parsing, file-linked diagnostics, leak detection, and
  schema-versioned command manifests recording command, working directory,
  environment subset, inputs, outputs, duration, exit code, and hashes.
- Shared core services for settings (with schema migration and a settings-file
  override), operation state, package archives with layered mounting of nested
  archives, package previews, project manifests, game installation profiles,
  compiler registry discovery, compiler wrapper profiles, editor profile
  presets, AI connector design stubs, and release metadata.
- Safe package write-back with staged add/import, replace, rename, delete,
  conflict/blocker reporting, package manifests, before/after composition, and
  deterministic save-as writers for PAK, ZIP/PK3, and WAD (PWAD/IWAD and
  WAD2/WAD3) outputs. Writing over the open source requires explicit in-place
  replacement and preserves a backup. Atomic publication keeps the original in
  place until commit; verified recovery copies and journals survive process
  interruption. `package recover` inspects retained files and can finish backup
  publication for an already-installed replacement. Interrupted Saves in the GUI
  and `package interrupted-saves` discover bounded folder metadata; selected saves
  are verified before reviewed completion. Pre-commit/changed-output decisions,
  power-loss durability and external-writer races remain open in the package audit.
- Syntax highlighting for configs, shader scripts, QuakeC, and entity
  definition text, themed from the active studio theme.
- Five themes including high-contrast dark and high-contrast light, applied
  across the shell, charts, map viewport, asset views, and code highlighting;
  text scale at 100/125/150/175/200%; density and reduced-motion preferences.
- Runtime localization: 20 target-language catalogs plus a pseudo-locale
  compiled to `.qm` by `lrelease`, installed at startup with right-to-left
  layout handling, and a `localization report` command covering pseudo,
  right-to-left, expansion ratio, layout checks, and stale-catalog reporting.
- High-DPI setup, a generated application icon, and a rotating session log that
  mirrors Qt warnings and above into the user's application data directory.
- CLI router with text and JSON output for project, package, map, BSP, build,
  launch, texture, shader, sprite, code, asset, install, compiler, editor
  profile, localization, diagnostics, extension, AI, about, and exit-code
  surfaces.
- License-clean Doom, Quake, and Quake III-family sample projects.
- Portable package generation for Windows, macOS, and Linux targets with
  package manifest, checksums, generated offline guide, platform smoke notes,
  samples, and VibeStudio/imported-compiler license bundle.
- Cross-platform PR CI hooks for build/test, an offscreen `--self-test` GUI
  smoke check that visits and repaints every work surface, CLI validation,
  sample validation, portable packaging validation, release asset validation,
  documentation version checks, and compiler submodule checks.

## Known Gaps

- No production level, model, audio, sprite, shader, code, or script
  editor yet. The map viewport is inspect-and-select with save-as edits, not a
  full editor.
- Texture authoring includes bounded layers, `.vtexture` projects, guarded saves,
  recovery, pixel/selection/seam tools, nine export profiles, native package
  staging and shared CLI services. Browser decoding, thumbnail caching, palette
  refresh and PNG publication use bounded background work. Project save preparation
  and large-file reads/checksums are cancellable before guarded publication.
  Cross-editor/native/compiler handoffs have generated-asset acceptance evidence.
  The current audit has passing results for all 42 relevant suites on Windows
  and Linux/WSL, enlarged/RTL accessibility metadata/layout checks, and a local
  Qt-deployed Windows package launch. A 22-step generated-asset workflow verifies
  Quake, Quake II and Quake III compiler/package handoffs. Physical input,
  screen readers, native windows, macOS/ARM, clean-machine deployment and game
  rendering remain unverified; see the
  [texture editor audit](plans/texture-editor-release-candidate.md).
- Models renders OBJ, MDL, MD2 and MD3 geometry with material, skin and frame
  inspection. Mesh authoring, UVs, animation, tags, collision, assemblies and
  package/level handoffs use shared GUI/CLI services. Professional modeller
  release acceptance remains open in the [modeller gate](MODELLER_RELEASE.md),
  including the complete platform, performance and physical accessibility audit.
  MDC, MDR and IQM still provide header metadata only.
- Audio playback depends on the build. With Qt Multimedia linked, the Audio
  page plays WAV, Doom DMX, and whatever compressed formats Qt's decoders
  handle; without it, previews stop at parsed headers and a waveform envelope.
  Browser preview and audition preparation run on bounded workers and share the
  editor's transport, source identity checks, cancellation and retryable device
  errors. WAV audition preserves precision; DMX uses PCM16 wrapping. Browser
  WAV export also accepts the documented MP3, native FLAC and Ogg Vorbis imports.
- Audio authoring now includes bounded PCM effects, undo, edited auditioning,
  package staging, lossless `.vsaudio` projects, atomic conflict-aware saves,
  local recovery, frame/sample navigation, a shared float clipboard, high-quality
  resampling, integer/float WAV precision, optional dither, Doom DMX/game sound
  delivery presets, per-channel/selection peak/RMS/DC and over-range analysis,
  true peak and integrated loudness with reviewed surround speaker roles,
  loop/cue authoring, bounded compressed import, startup recovery discovery and
  inventory, Quake II/III level sound handoff, and shared CLI operations.
  Professional audio release acceptance
  remains open in the [audio editor audit](plans/audio-editor-release-candidate.md),
  including final current-source regression, broader performance, physical
  device/accessibility acceptance and native cross-platform evidence.
- No text-to-speech. The TTS preference is stored and reported by the CLI and
  setup summary, but no speech engine is wired up.
- No perspective 3D view. Map previews are orthographic only.
- No entity definition (FGD/DEF/ENT) parsing. Those files are syntax
  highlighted but not loaded into an entity model.
- No missing-texture detection. Map texture references are listed but not
  resolved against the textures in mounted packages.
- Package-manager release acceptance is still open: persistent staging,
  undo/redo, a consistent staged browser, asynchronous open/preview/extraction,
  save recovery hardening and native cross-platform evidence remain in the
  [package-manager audit](plans/package-manager-release-candidate.md). Confirmed
  in-place saves and background comparison/review are implemented.
- WAD2/WAD3 compressed lumps are listed but not decoded.
- No fuzz targets. The binary parsers are fixture-tested, including truncated,
  malformed, and hostile inputs, but nothing is fuzzed.
- No clean-machine launch verification. CI runs the GUI self-test offscreen on
  the build machine; the clean-machine steps in the portable package notes have
  not been executed.
- No measured accessibility audit. Both high-contrast themes and all five text
  scales are selectable and persist, but layout has not been verified at each
  scale and contrast has not been measured.
- No source-port installation detection yet.
- AI provider network calls remain opt-in future work; current AI workflows are
  safe, no-write, manifest-backed experiments and connector configuration.
- Windows packaging now verifies the selected Qt runtime and matching source
  companion; hosted CI execution of the paired-artifact workflow remains to be
  verified. macOS/Linux still require native Qt deployment. Signing,
  notarization, vendor-runtime source reconstruction, clean-machine acceptance
  and durable binary/source publication remain open before production release.

## Verification

The release-candidate gate is:

```sh
pwsh -NoProfile -File scripts/meson_build.ps1
```

That script builds the app, runs Meson tests, validates the CLI, validates all
sample projects, validates current-platform packaging, and runs the full release
asset gate for Windows, macOS, and Linux target package manifests.

## Artifact Plan

Use `scripts/package_portable.py` to stage a local portable package:

```sh
python scripts/package_portable.py --binary builddir/src/vibestudio --archive
```

Use `--target-platform all` to stage all target package shapes from the
available binary, and use `builddir/src/vibestudio.exe` on Windows. Publishing,
tags, signed release artifacts, and final deployment remain manual/future
release-management steps.
