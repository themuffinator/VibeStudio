# Portable Workspaces

**File → Save Workspace As…** writes a `.vibeworkspace` file. **File → Open
Workspace…**, drag and drop, and a command-line file argument restore it.
Both actions are available through the command palette. The file joins the
current project, package or package draft, map (including its WAD map name),
saved code tabs, selected texture/model/audio entries, and active studio module.

This is a versioned UTF-8 JSON format owned by VibeStudio. It references saved
work; it does not contain asset payloads or replace editor saves. Save a texture
as `.vtexture`, audio as `.vsaudio`, meshes as `.mesh.json`, assemblies as
`.assembly.json`, and package drafts as `.vibepackage`. Each keeps its existing
validation, undo and recovery rules. The project's `.vibestudio/project.json`
continues to own build folders, compiler and installation choices; a workspace
selects that project instead of copying those settings.

## Restoring work

The project is restored first, then the package, map and code tabs, followed by
asset selections and the active module. Existing dirty package/map guards still
apply. Already-open code tabs keep their unsaved edits. Opening adds saved tabs
to the current session; it does not close unrelated tabs or authoring dialogs.
Missing references are reported, and available references can still open.
An asset selection is restored only when its package entry is unambiguous.
Saved package drafts resume through the ordinary package document service.

Workspace creation never starts a compiler, launches a game, installs anything,
or runs a command from the file. It stores no AI credentials or machine settings.
Unsaved buffers, temporary package-entry copies, preview worker state, undo
history, camera poses, and open asset-authoring dialogs are not captured in v1.
Saved level views and each editor's recovery remain separate. Save authoring
documents before closing them; reopen those sources through their editor.

## Format contract

```json
{
  "format": "vibestudio-workspace",
  "version": 1,
  "project": "game",
  "package": "game/assets",
  "map": "game/maps/arena.map",
  "mapName": "",
  "codeFiles": ["game/scripts/arena.shader"],
  "currentCodeFile": "game/scripts/arena.shader",
  "activeModule": "textures",
  "assetSelections": {"textures": "textures/arena/wall.dds"},
  "extensions": {}
}
```

`format` and integer `version` are required. Other fields are optional; paths
and lists default empty, and `activeModule` defaults to `workspace`. Paths are
relative to the workspace file, not the process working directory. Writers use
relative paths where possible, including `../` references to nearby projects.
Moving a directory tree containing both the workspace and its referenced files
preserves those links. External absolute references remain machine-specific;
moving only the workspace requires relocating its references. URI, device and
drive-relative paths are rejected. Reference inspection does not read asset
payloads or modify the referenced files.

Module IDs are `workspace`, `levels`, `models`, `textures`, `audio`, `packages`,
`code`, `shaders`, `build` and `settings`. Asset selections accept only
`textures`, `models` and `audio`, require a package reference, and use safe
package virtual paths. `currentCodeFile` must appear in `codeFiles`.

The reader admits at most 1 MiB, 128 code files, 4096 characters per path and
64 characters in `mapName`. It validates types and rejects unsupported versions
and unknown top-level fields. Custom metadata belongs in `extensions`, whose
JSON object is retained through load/save and Save As. No extension is executed.
The same size bound includes extension data. Failed parsing leaves the caller's
previous document untouched.

Saves take a destination lock and check the previously reviewed SHA-256
revision. New files publish without replacing an existing destination;
replacement uses an atomic save with no direct-write fallback. The destination
folder must exist and a symbolic-link destination is refused. Existing files
must be compatible workspaces. An external edit invalidates the shell's saved
revision; reopen and review it or save elsewhere. A noncooperating writer can
still race the final check on an existing file, as with the other document
writers; the lock coordinates VibeStudio writers.

## CLI

```sh
vibestudio --cli workspace create ./work.vibeworkspace \
  --project ./game --package ./game/assets --map ./game/maps/arena.map \
  --code ./game/scripts/arena.shader --active-module textures --json
vibestudio --cli workspace inspect ./work.vibeworkspace --json
vibestudio --cli workspace create ./session.vibeworkspace --from-session --dry-run
```

Repeat `--code` to include more tabs; `--current-code` selects one, and
`--map-name` identifies a map inside a WAD. `--from-session` reads the last
persisted session and current project from settings, then applies explicit
options. It cannot capture unsaved content from a running GUI. `--overwrite`
permits replacement of a reviewed compatible workspace and retains its
extensions. `--dry-run` validates and reports without creating a file or lock.

Inspection returns relative references, a revision and `missingReferences`.
Missing referenced assets are diagnostics, not a schema failure. Missing input
workspace files return exit 3; malformed/unsupported files return exit 4;
invalid CLI options return exit 2. The shell and CLI share
`core/workspace_document`; there is no second workspace serializer.

`workspace-smoke` covers relocation, bounds, extensions, stale saves, invalid
inputs, and CLI behavior. `workspace-ui-smoke` exercises shell restoration and
capture using a high-contrast theme, 150% text and expanded translations, with
no keyboard/mouse injection or operating-system capture.
