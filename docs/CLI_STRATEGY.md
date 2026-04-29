# CLI Strategy

The VibeStudio CLI is a first-class interface, not a debugging afterthought.
Every workflow that can reasonably run headless should have a stable command
surface using the same core services as the GUI.

## Goals
- [ ] Use the same project, package, compiler, validation, and task services as the GUI.
- [ ] Provide human-readable output by default.
- [ ] Provide JSON output for automation.
- [ ] Use stable exit codes.
- [ ] Support reproducible command manifests.
- [ ] Support CI, release, and batch workflows.
- [ ] Keep help text aligned with README and generated user docs.

## Command Families

### Project
- [ ] `project create`
- [ ] `project info`
- [ ] `project validate`
- [ ] `project set-install`
- [ ] `project list-mounts`

### Installations
- [ ] `install list`
- [ ] `install detect`
- [ ] `install add`
- [ ] `install validate`
- [ ] `install select`

### Packages
- [ ] `package info`
- [ ] `package list`
- [ ] `package extract`
- [ ] `package validate`
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
- [ ] `compiler list`
- [ ] `compiler detect`
- [ ] `compiler probe`
- [ ] `compiler run`
- [ ] `compiler rerun`
- [ ] `compiler manifest`
- [ ] `compiler explain-log`

### AI Automation
- [ ] `ai status`
- [ ] `ai explain-log`
- [ ] `ai propose-command`
- [ ] `ai propose-manifest`
- [ ] `ai apply-staged`

### Release And QA
- [ ] `qa smoke`
- [ ] `qa support-matrix`
- [ ] `release manifest`
- [ ] `credits validate`
- [ ] `docs validate`

## Output Contract
- [ ] `--json` emits stable machine-readable output.
- [ ] `--quiet` suppresses non-error narration.
- [ ] `--verbose` includes diagnostics and timing.
- [ ] `--manifest <path>` writes command manifests.
- [ ] `--dry-run` shows planned changes without writing.
- [ ] Non-zero exit codes distinguish usage errors, validation failures, operation failures, and crashes.

## UX Rules
- [ ] Every destructive command must have a dry-run or staged mode.
- [ ] Every command that writes files must report exact output paths.
- [ ] Commands should accept project-relative and absolute paths.
- [ ] Commands should work with spaces and Unicode in paths.
- [ ] Help examples should cover Windows PowerShell and POSIX shells where syntax differs.
