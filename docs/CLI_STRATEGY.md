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
  installation, compiler, and exit-code commands.
- [x] Stable exit-code contract exposed through `--exit-codes` and
  `cli exit-codes`.
- [x] Schema-versioned command manifest writer for compiler command plans.
- [x] Schema-versioned command manifest loader and re-run path for compiler runs.
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
- [ ] `package manifest`
- [ ] `package compare`
- [ ] `package stage`
- [ ] `package save-as`

### Assets
- [ ] `asset inspect`
- [ ] `asset preview-export`
- [ ] `asset convert`
- [ ] `asset graph`
- [ ] `asset search`

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
- [ ] `ai apply-staged`

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
- [x] `--dry-run` shows planned package extraction writes and compiler command runs without touching files.
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
vibestudio --cli compiler run ericw-qbsp --input ".\maps\start.map" --watch --manifest ".\build\start.run.json"
vibestudio --cli ai explain-log --log ".\build\qbsp.log" --json
```

POSIX shells:

```sh
vibestudio --cli package list './baseq3/pak0.pk3' --json
vibestudio --cli compiler plan ericw-qbsp --input './maps/start.map' --dry-run
vibestudio --cli ai propose-command --prompt 'build quake map maps/start.map with qbsp'
```
