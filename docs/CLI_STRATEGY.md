# CLI Strategy

The VibeStudio CLI is a first-class interface, not a debugging afterthought.
Every workflow that can reasonably run headless should have a stable command
surface using the same core services as the GUI.

## Goals
- [x] Use the same project, package, compiler, validation, and task services as the GUI.
- [x] Provide human-readable output by default.
- [x] Provide JSON output for automation.
- [x] Use stable exit codes.
- [x] Support reproducible command manifests.
- [x] Support CI, release, and batch workflows.
- [x] Keep help text aligned with README and generated user docs.

## Command Architecture
- [x] Lightweight in-process router for early subcommand families.
- [x] Global `--json` output mode for router-backed project, package,
  installation, asset, map, shader, sprite, code, extension, compiler, AI, and
  exit-code commands.
- [x] Stable exit-code contract exposed through `--exit-codes` and
  `cli exit-codes`.
- [x] Schema-versioned command manifest writer for compiler command plans.
- [x] Schema-versioned command manifest loader and re-run path for compiler runs.
- [x] Shared Advanced Studio command services for shader scripts, sprite plans,
  code indexing, extension manifests, and reviewable AI creation proposals.
- [x] Evaluate CLI11 and adopt an in-process command registry that exposes
  testable command metadata through `cli commands`; keep the external CLI11
  parser dependency deferred until generated help, validation, and shell
  completion justify it.

## Command Families

### About And Credits
- [x] `--about`
- [x] `--credits`
- [x] `about`
- [x] `credits validate`

### Project
- [x] `--project-init <path>` scaffold project manifest creation/update.
- [x] `--project-info <path>` scaffold manifest and health summary output.
- [x] `--project-validate <path>` scaffold project health validation.
- [ ] `project create`
- [x] `project info`
- [x] `project validate`
- [ ] `project set-install`
- [ ] `project list-mounts`

### Installations
- [x] `--installations-report` scaffold report for saved manual installation profiles.
- [x] `--add-installation <root>` scaffold manual profile creation.
- [x] `--select-installation <id>` scaffold selected-profile persistence.
- [x] `--validate-installation <id>` scaffold read-only validation.
- [x] `--remove-installation <id>` scaffold profile removal without touching files.
- [x] `--detect-installations` read-only Steam/GOG candidate detection.
- [x] `install list`
- [x] `install detect`
- [ ] `install add`
- [ ] `install validate`
- [ ] `install select`

### Packages
- [x] `--package-formats` scaffold report for package/archive interface descriptors.
- [x] `--check-package-path <path>` scaffold validation for normalized safe package virtual paths.
- [x] `--info <path>` scaffold package summary for folders, PAK, WAD, ZIP, and PK3.
- [x] `--list <path>` scaffold package entry listing for folders, PAK, WAD, ZIP, and PK3.
- [x] `--preview-package <path> --preview-entry <virtual-path>` scaffold package
  entry text, image metadata, and binary preview output.
- [x] `--extract <path> --output <folder>` safe selected/all package extraction
  with repeatable `--extract-entry`, `--dry-run`, and explicit `--overwrite`.
- [x] `--validate-package <path>` read-only package loader validation.
- [x] `package info`
- [x] `package list`
- [x] `package preview`
- [x] `package extract`
- [x] `package validate`
- [x] `package manifest`
- [ ] `package compare`
- [x] `package stage`
- [x] `package save-as`

### Assets
- [x] `asset inspect`
- [ ] `asset preview-export`
- [x] `asset convert`
- [x] `asset audio-wav`
- [ ] `asset graph`
- [x] `asset find`
- [x] `asset replace`
- [x] `asset search` through the `asset find` alias.

### Maps
- [x] `map inspect` for Doom WAD map lumps and Quake-family `.map` files.
- [x] `map edit` for entity key/value edits with non-destructive `--output`.
- [x] `map move` for Doom vertices/linedefs/things and Quake entities.
- [x] `map compile-plan` for profile-backed compiler command review.
- [x] JSON output for map statistics, entities, brushes, textures, validation,
  preview lines, selection, properties, and save reports.

### Shaders
- [x] `shader inspect` for idTech3 `.shader` parsing, stage graphs, stage
  previews, raw text detail, and mounted-package texture-reference validation.
- [x] `shader set-stage` for non-destructive stage directive edits with
  `--output`, `--dry-run`, `--overwrite`, and JSON save reports.

### Sprites
- [x] `sprite plan` for Doom lump naming, Quake `.spr` sequencing, palette
  preview notes, frame rotations, and package staging paths.

### Code IDE
- [x] `code index` for source tree discovery, language hook descriptors,
  diagnostics, symbol search, compiler task suggestions, and launch profiles.

### Compilers
- [x] `--compiler-registry` scaffold compiler registry and executable discovery.
- [x] `compiler list`
- [x] `compiler profiles`
- [x] `compiler plan`
- [x] `compiler set-path`
- [x] `compiler clear-path`
- [ ] `compiler detect`
- [ ] `compiler probe`
- [x] `compiler run`
- [x] `compiler rerun`
- [x] `compiler manifest`
- [x] `compiler copy-command`
- [ ] `compiler explain-log`

### Editor Profiles
- [x] `--editor-profiles` scaffold report for placeholder editor interaction profiles.
- [x] `--set-editor-profile <id>` scaffold selected profile persistence.
- [x] `editor profiles`
- [x] `editor current`
- [x] `editor select`

### AI Automation
- [x] `--ai-status`
- [x] `ai status`
- [x] `ai connectors`
- [x] `ai tools`
- [x] `ai explain-log`
- [x] `ai propose-command`
- [x] `ai propose-manifest`
- [x] `ai package-deps`
- [x] `ai cli-command`
- [x] `ai fix-plan`
- [x] `ai asset-request`
- [x] `ai compare`
- [x] `ai shader-scaffold`
- [x] `ai entity-snippet`
- [x] `ai package-plan`
- [x] `ai batch-recipe`
- [x] `ai review`
- [ ] `ai apply-staged`

### Extensions
- [x] `extension discover` for `vibestudio.extension.json` manifests across one
  or more roots.
- [x] `extension inspect` for manifest, trust, sandbox, command, capability,
  and staged generated-file reporting.
- [x] `extension run` for command-plan review, dry-run-first execution, extra
  arguments, and staged generated-file summaries.

### Release And QA
- [x] `scripts/validate_samples.py`
- [x] `scripts/validate_packaging.py`
- [x] `scripts/validate_release_assets.py`
- [x] `scripts/package_portable.py`
- [ ] `qa smoke`
- [ ] `qa support-matrix`
- [ ] `release manifest`
- [x] `credits validate`
- [ ] `docs validate`

## Output Contract
- [x] `--json` emits stable machine-readable output for supported command families.
- [x] `--quiet` suppresses non-error narration.
- [x] `--verbose` includes diagnostics and timing.
- [x] `--manifest <path>` writes compiler command manifests.
- [x] `--dry-run` shows planned package extraction writes, package save-as
  writes, shader save reports, extension command plans, and compiler command
  runs without touching files.
- [x] `--watch` streams compiler task log entries while long-running process-backed commands are active.
- [x] `--task-state` adds automation-friendly task-state objects to JSON output where supported.
- [x] Non-zero exit codes distinguish usage errors, not-found cases,
  validation failures, operation failures, and unavailable workflows.

## UX Rules
- [x] Every active destructive command must have a dry-run or staged mode.
- [x] Every active command that writes files must report exact output paths.
- [ ] Commands should accept project-relative and absolute paths.
- [ ] Commands should work with spaces and Unicode in paths.
- [x] Help examples should cover Windows PowerShell and POSIX shells where syntax differs.

## Current Examples

PowerShell:

```powershell
vibestudio --cli package validate "C:\Games\Quake\id1\pak0.pak" --json
vibestudio --cli package save-as ".\mod-folder" ".\build\mod.pk3" --format pk3 --add-file ".\autoexec.cfg" --as "scripts/autoexec.cfg" --manifest ".\build\mod.manifest.json"
vibestudio --cli map edit ".\maps\start.map" --entity 1 --set targetname=lift --output ".\maps\start-edited.map"
vibestudio --cli shader set-stage ".\scripts\common.shader" --shader "textures/base/wall" --stage 1 --directive blendFunc --value "GL_ONE GL_ONE" --output ".\scripts\common-edited.shader" --json
vibestudio --cli sprite plan --engine doom --name TROO --frames 2 --rotations 8 --palette doom --json
vibestudio --cli compiler run ericw-qbsp --input ".\maps\start.map" --watch --manifest ".\build\start.run.json"
vibestudio --cli extension discover ".\extensions" --json
vibestudio --cli ai explain-log --log ".\build\qbsp.log" --json
vibestudio --cli ai review --kind shader --prompt "glowing gothic wall" --json
```

POSIX shells:

```sh
vibestudio --cli package list './baseq3/pak0.pk3' --json
vibestudio --cli package stage './mod-folder' --add-file './autoexec.cfg' --as 'scripts/autoexec.cfg' --json
vibestudio --cli map inspect './maps/start.map' --select entity:0 --json
vibestudio --cli shader inspect './scripts/common.shader' --package './baseq3' --json
vibestudio --cli code index './mymod' --find monster --json
vibestudio --cli compiler plan ericw-qbsp --input './maps/start.map' --dry-run
vibestudio --cli extension run './extensions/sample/vibestudio.extension.json' make-file --dry-run --json
vibestudio --cli ai propose-command --prompt 'build quake map maps/start.map with qbsp'
vibestudio --cli ai batch-recipe --prompt 'convert doom sprites to indexed png' --json
```
