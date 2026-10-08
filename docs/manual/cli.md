# Command line

VibeStudio includes a full command line that runs the same services as the studio. Use it to script
package checks, builds and conversions, to run VibeStudio in CI, or to inspect files without opening
the studio. This page shows how to run it, the options every command shares, and how to use its
output in scripts.

> [!NOTE]
> **Status: Available.** The command line covers every studio area, from projects and packages to
> maps, models, builds and diagnostics, and shares its services and tests with the studio. Like the
> rest of VibeStudio, it has had little real-world use so far.

## Run the command line

The command line is the studio program itself, started with `--cli`:

```text
vibestudio --cli [--json] <family> <command> [arguments]
```

| How you got VibeStudio | Program to run |
| --- | --- |
| Windows installer or portable zip | `vibestudio.exe` in the `bin` folder where you installed or extracted VibeStudio |
| macOS disk image | `/Applications/VibeStudio.app/Contents/MacOS/vibestudio` |
| Linux AppImage | `./VibeStudio-<version>-linux-x86_64.AppImage` |
| Nightly package on macOS or Linux | `bin/vibestudio` in the folder you extracted |
| Built from source | `builddir/src/vibestudio`, or `builddir\src\vibestudio.exe` on Windows; see [Build from source](building-from-source.md) |

The examples in this manual write `vibestudio`. Replace it with the program's path, or add its folder
to your PATH. For example:

```sh
/Applications/VibeStudio.app/Contents/MacOS/vibestudio --cli --version
./VibeStudio-<version>-linux-x86_64.AppImage --cli package info ./id1/pak0.pak
```

`vibestudio --cli` on its own, or with `--help`, prints the full help: the shared options, then every
command with an example. Each command there lists the options it supports in brackets, such as
`[--json --dry-run]`. Quote paths that contain spaces, as in PowerShell:

```powershell
vibestudio --cli package validate "C:\Games\Quake\id1\pak0.pak" --json
```

## Options every command shares

Options can go anywhere after `--cli`.

| Option | What it does |
| --- | --- |
| `--help` | Prints the full help and exits. |
| `--version` | Prints the version and exits. |
| `--json` | Prints one JSON object instead of text. Every command supports it. |
| `--quiet` | Hides the text output of a successful command; errors still go to standard error. Ignored with `--json`. |
| `--verbose` | Adds timing and extra diagnostics to text output. Ignored with `--json`. |
| `--settings-file <path>` | Reads and writes settings in this INI file instead of your own settings. |
| `--dry-run` | Shows what a command that writes files would do, without writing. |
| `--overwrite` | Lets a command replace an existing output file. |
| `--watch` | Streams progress lines from `build run`, `compiler run` and `compiler rerun` in text output. Nothing is streamed with `--json`. |
| `--task-state` | Adds task-state objects to JSON output where supported. |
| `--exit-codes` | Prints the exit-code table below. |

The command line uses the same settings as the studio, so commands such as `install add`,
`compiler set-path` or `editor select` change what the studio sees. In scripts and CI, pass
`--settings-file` with a file of your own to keep them apart. `vibestudio --cli --settings-report`
prints where the settings in use are stored.

## Command families

Commands are grouped into families. `vibestudio --cli cli commands` prints the complete, current
list, and `--json` adds whether each command supports `--json`, `--dry-run` and `--watch`.

| Family | Covers |
| --- | --- |
| `cli` | The command list and the exit-code table |
| `ui` | Status chips, default keyboard shortcuts and command palette entries |
| `project` | Project manifests, health checks and file lists |
| `install` | Game installation profiles and Steam and GOG detection |
| `workspace` | `.vibeworkspace` files |
| `package` | Inspecting, extracting, validating, comparing, staging and saving packages, drafts and recovery |
| `asset` | Format capabilities, image conversion, audio editing, and project text search and replace |
| `map` | Inspecting, editing, rendering and generating maps, their textures and dependencies |
| `entity` | Entity definition catalogues and map entity validation |
| `editor` | Editor profiles, gestures, keys, view layout, saved views and map layers |
| `model` | Inspecting, importing, editing, building and exporting models |
| `texture` | Decoding, creating, editing, exporting, staging and generating textures |
| `shader` | Quake III shader scripts |
| `material` | Textures, shaders and materials of idTech 1 to 4: list, check, render, edit |
| `sprite` | Doom and Quake sprite plans |
| `code` | Code file lists, local language servers and format-preserving text saves |
| `compiler` | Compiler discovery, paths, single compiler runs and command manifests |
| `build` | Build pipelines and prepared builds |
| `bsp` | Compiled map inspection |
| `launch` | Game launch plans and launching |
| `localization` | Language targets and translation catalogue status |
| `accessibility` | Accessibility settings, speech voices and a speech test |
| `diagnostics` | Support bundles and crash reports |
| `ai` | Optional AI: status, connectors and reviewable proposals |
| `extension` | Extension manifests |
| `about`, `credits` | Version and credits, and credits validation |

## Common recipes

List every command:

```sh
vibestudio --cli cli commands
```

Inspect, filter and validate a package:

```sh
vibestudio --cli package info ./id1/pak0.pak
vibestudio --cli package list ./id1/pak0.pak --where "ext=wav"
vibestudio --cli package validate ./release.pk3 --json
```

Render a map to an SVG picture, optionally with a compiler leak trail. The projection is `top`,
`front` or `side`; without `--output` the SVG goes to standard output.

```sh
vibestudio --cli map render ./maps/start.map --output ./start.svg
vibestudio --cli map render ./maps/start.map --projection front --leak ./maps/start.pts --output ./start-leak.svg --overwrite
```

Plan a build without running anything, then run it with live progress:

```sh
vibestudio --cli build list
vibestudio --cli build plan quake-full --input ./maps/start.map
vibestudio --cli build run quake-full --input ./maps/start.map --watch
```

Write a diagnostics bundle to `./diagnostics/vibestudio-diagnostics.json` for a bug report:

```sh
vibestudio --cli diagnostics bundle --output ./diagnostics
```

List the target languages, then report on one of them:

```sh
vibestudio --cli localization targets
vibestudio --cli localization report --locale ar --json
```

Print the status chips with their non-colour signals, the default shortcuts with any conflicts, and
the command palette entries:

```sh
vibestudio --cli ui semantics
```

More examples are on the pages for each area, such as [Packages](packages.md) and
[Build and launch](build-and-launch.md).

## Use JSON output in scripts

With `--json`, a command prints one JSON object on standard output, even when it fails. Every object
has the same core fields, plus fields of its own:

| Field | Meaning |
| --- | --- |
| `command` | The command that ran, such as `package info`. |
| `ok` | `true` only when the exit code is 0. |
| `status` | `success` or `error`. |
| `exitCode` | The exit code as an object with `code`, `id`, `label` and `description`. |
| `message` | What went wrong, when the command failed. |

For example, a package that does not exist:

```json
{
    "command": "package info",
    "exitCode": {
        "code": 1,
        "description": "The command was understood but the operation failed.",
        "id": "failure",
        "label": "Operation failure"
    },
    "message": "Unable to open package: Package path does not exist.",
    "ok": false,
    "status": "error"
}
```

Without `--json`, results go to standard output and errors to standard error.

## Exit codes

| Code | ID | Meaning |
| --- | --- | --- |
| 0 | `success` | The command completed successfully. |
| 1 | `failure` | The command was understood but the operation failed. |
| 2 | `usage-error` | Arguments were missing, malformed or incompatible. |
| 3 | `not-found` | A requested project, package, entry, installation or tool was not found. |
| 4 | `validation-failed` | Validation completed and found blocking problems. |
| 5 | `unavailable` | The workflow is recognised but no capable implementation or tool is available yet. |

A check that finds a problem exits with 4, not 1, so a script can tell "the check failed" from "the
command failed". For example, `package validate`, `package compare` and `entity validate` exit with 4
when they find problems or differences:

```sh
vibestudio --cli package validate ./release.pk3 --json > validation.json
if [ $? -eq 4 ]; then echo "release.pk3 has problems"; fi
```

```powershell
vibestudio --cli package validate .\release.pk3 --json | Out-File validation.json
if ($LASTEXITCODE -eq 4) { Write-Host "release.pk3 has problems" }
```

<details>
<summary>Single-option reports</summary>

These options print a report and exit:

| Option | Prints |
| --- | --- |
| `--platform-report` | Platform and Qt runtime details |
| `--settings-report` | Where settings are stored, and recent projects |
| `--preferences-report` | Accessibility and language preferences |
| `--setup-report` | First-run setup status |
| `--compiler-registry` | Compiler discovery, the same as `compiler list` |
| `--localization-report` | Localization targets and catalogue status |

</details>

## Learn more

- [CLI strategy](../CLI_STRATEGY.md): the full reference for every family, option and output field.
- [Troubleshooting and FAQ](troubleshooting.md): logs, diagnostics bundles and crash reports.
