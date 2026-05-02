# VibeStudio 0.1.0-rc1

This is the first MVP release-candidate cut for the foundation scaffold. It is
not a production-ready editor suite; it is a validated baseline for the
project, package, compiler, settings, sample, packaging, credits, and CLI
surfaces that later editor work can build on.

## Included

- Qt6 Widgets shell with workspace dashboard, setup panel, preferences,
  activity center, inspector details, package browser with tree/list views,
  graphical package composition summary, compiler pipeline readiness summary,
  workspace problems/search/changes/timeline panels, Steam/GOG installation
  candidate detection/import, safe package extraction, and
  About/Credits/license surface.
- Shared core services for settings, operation state, package archives,
  package previews, project manifests, game installation profiles, compiler
  registry discovery, compiler wrapper profiles, editor profile presets, AI
  connector design stubs, and release metadata.
- CLI router with text and JSON output for project, package inspection,
  package extraction/validation, installation, compiler, editor profile, AI,
  about, and exit-code surfaces.
- Structured compiler command plans and schema-versioned command manifests.
- License-clean Doom, Quake, and Quake III-family sample projects.
- Portable package generation for Windows, macOS, and Linux targets with
  package manifest, checksums, generated offline guide, platform smoke notes,
  samples, and VibeStudio/imported-compiler license bundle.
- Cross-platform PR CI hooks for build/test, CLI validation, sample validation,
  portable packaging validation, release asset validation, documentation
  version checks, and compiler submodule checks.

## Known Gaps

- No production level, model, texture, audio, sprite, shader, code, or script
  editor yet.
- No package write-back, package diffing, or package save-as.
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
