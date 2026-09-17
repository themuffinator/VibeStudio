# VibeStudio 0.1.0-rc1

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
  ZIP/PK3 writing emits stored or fixed-Huffman deflate.
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
  WAD2/WAD3) outputs. Writing back over the open source package is blocked.
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

- No production level, model, texture, audio, sprite, shader, code, or script
  editor yet. The map viewport is inspect-and-select with save-as edits, not a
  full editor.
- No model geometry rendering. The Models surface decodes header metadata,
  skins, and frame/surface counts and shows a skin image plus a text viewport
  summary; no mesh is drawn.
- No audio playback. Audio previews stop at parsed headers (WAV, Ogg, MP3,
  FLAC) and a waveform envelope; no audio backend is linked, so the analysis
  only flags whether an entry would be a playback candidate. Convert-to-WAV
  handles PCM sources and refuses compressed ones with a missing-decoder
  message.
- No text-to-speech. The TTS preference is stored and reported by the CLI and
  setup summary, but no speech engine is wired up.
- No perspective 3D view. Map previews are orthographic only.
- No entity definition (FGD/DEF/ENT) parsing. Those files are syntax
  highlighted but not loaded into an entity model.
- No missing-texture detection. Map texture references are listed but not
  resolved against the textures in mounted packages.
- No in-place package overwrite workflow and no package compare tooling; the
  staged save-as path is the only write route.
- The deflate encoder emits stored or fixed-Huffman blocks only; there is no
  dynamic-Huffman encoder, so PK3 output is larger than a `zlib`-produced
  archive.
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
- Portable packages still require platform Qt deployment, signing,
  notarization, and published artifact promotion before they become production
  release downloads.

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
