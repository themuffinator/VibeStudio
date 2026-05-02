# VibeStudio Offline User Guide (0.1.0-rc1)

This generated guide is bundled with portable release artifacts so a user can
configure, inspect, validate, and troubleshoot the MVP without network access.
It summarizes the release-candidate workflows; the full source documentation
remains in the `docs/` directory.

## What This MVP Is

VibeStudio is currently an integrated foundation for idTech1, idTech2, and
idTech3 projects. It provides a Qt6 Widgets shell, first-run setup scaffold,
project manifests, read-only package browsing and extraction, compiler command
planning/runs, sample projects, safe AI workflow stubs, credits visibility, and
a CLI for repeatable automation.

The MVP is honest about unfinished surfaces: level, model, texture, audio,
sprite, shader, package write-back, and code-editing tools are still roadmap
work. Existing write/export operations report exact output paths and keep raw
details, logs, and manifests inspectable.

## First Launch Checklist

1. Start VibeStudio.
2. Complete, skip, or resume first-run setup.
3. Choose language, theme, text scale, density, reduced motion, and optional
   OS-backed TTS preferences.
4. Select an editor profile or keep the VibeStudio default.
5. Add or detect a game installation, or defer installation setup.
6. Open or initialize a project folder.
7. Confirm the Activity Center and Inspector show current state and warnings.

All setup choices are stored in normal settings and can be edited later from
Preferences or the CLI. AI-free mode is enabled by default and core workflows do
not require cloud services.

## Package Workflows

Supported read-only package inputs are folders, Quake PAK, Doom WAD, ZIP, and
PK3 archives.

Common CLI commands:

```sh
vibestudio --cli package info ./id1/pak0.pak --json
vibestudio --cli package list ./baseq3/pak0.pk3 --json
vibestudio --cli package validate ./maps.wad
vibestudio --cli package extract ./pak0.pak --output ./out --dry-run
```

In the GUI, package open and extraction operations create Activity Center tasks
with loading/progress/result states, warnings, cancellation where available,
and exact output paths.

## Project And Compiler Workflows

Use project manifests to keep source folders, package folders, outputs,
installation choices, AI settings, and compiler overrides in one place.

Common CLI commands:

```sh
vibestudio --cli project init ./samples/projects/quake-minimal
vibestudio --cli project info ./samples/projects/quake-minimal --json
vibestudio --cli compiler profiles --json
vibestudio --cli compiler plan ericw-qbsp ./samples/projects/quake-minimal/maps/start.map --dry-run --json
vibestudio --cli compiler run ericw-qbsp ./samples/projects/quake-minimal/maps/start.map --dry-run --task-state --json
```

Compiler runs capture command manifests, stdout/stderr, diagnostics, duration,
exit code, task-log entries, registered output paths, and hashes where outputs
exist. If a compiler executable is missing, the plan and run reports explain
which tool is unavailable and what path was checked.

## AI And Automation

AI features are optional and provider-neutral. The current MVP exposes safe
workflow manifests and no-write experiments for explaining compiler logs,
proposing commands, drafting manifests, suggesting package dependencies,
preparing asset requests, and comparing provider outputs.

Common CLI commands:

```sh
vibestudio --cli ai status --json
vibestudio --cli ai tools --json
vibestudio --cli ai explain-log --text "qbsp failed: leak near entity 1" --json
```

AI workflows report provider/model choices, context summaries, staged actions,
validation, cancellation state, and final summaries. They must not mutate
project files without review and explicit approval.

## Accessibility And Localization Smoke Checks

Before publishing a release asset, verify:

- Text scale presets: 100%, 125%, 150%, 175%, and 200%.
- High-contrast dark and high-contrast light themes.
- Keyboard-only setup completion or skip/resume.
- Pseudo-localization and right-to-left smoke modes for Arabic and Urdu.
- TTS test phrase and one task-result announcement where OS support exists.
- Non-color-only status labels for project, package, compiler, AI, and setup
  states.

## Release Bundle Contents

Portable release assets include:

- `bin/` with the staged VibeStudio executable.
- `README.md`, `VERSION`, and `docs/`.
- `docs/OFFLINE_USER_GUIDE.md`.
- `samples/` with license-clean Doom, Quake, and Quake III-family projects.
- `licenses/THIRD_PARTY_LICENSES.md` plus VibeStudio and imported compiler
  license files.
- `platform/README.txt` with target-specific launch notes.
- `package-manifest.json` and `CHECKSUMS.sha256`.

## Troubleshooting

- If a package fails to open, inspect the Activity Center task details and run
  `vibestudio --cli package validate <path> --json`.
- If a compiler profile is unavailable, run `vibestudio --cli compiler list
  --json` and configure a tool path override.
- If setup was skipped, reopen the setup panel or use the CLI setup commands to
  resume, advance, complete, or reset it.
- If AI must stay disabled, keep AI-free mode enabled; package, compiler,
  project, validation, CLI, and launch/test preparation workflows still work.
- If a release artifact is being checked on a clean machine, run the commands in
  `platform/README.txt` and compare the results with `package-manifest.json`.
