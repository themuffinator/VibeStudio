# Editor Profiles

VibeStudio's level editor should be adaptable enough to feel familiar to users
coming from established idTech editors. The goal is not to clone assets or
inherit bugs; the goal is to offer compatible mental models for layout,
navigation, selection, camera movement, grid behavior, and common shortcuts.

## Philosophy
- Editor familiarity is a product feature.
- One underlying scene/model/editing core should support multiple interaction profiles.
- Profiles should be data-driven where practical.
- Users should be able to switch profiles per project or globally.
- Profile choices should affect layout and controls, not file compatibility.
- Each profile must stay documented, credited, and testable.

## What Is Actually Implemented

The registry in `src/core/editor_profiles.*` is real data shared by the Qt
preferences surface, the inspector drawer, the CLI, settings storage, and smoke
tests. What it does and does not do:

**Live.** Each profile declares a list of bindings. A binding whose `commandId`
is a real shell command from `core/studio_semantics.h` — `map.open`,
`project.open`, `compiler.run`, `build.run-pipeline`, `shell.command-palette`,
`compiler.copy-cli`, and so on — names a command the shell genuinely has.

**Computed, not asserted.** `EditorProfileBinding::implemented` is set from
`shellCommandIdExists(commandId)` at construction time. Nothing in the registry
can claim a binding is routed when the command id does not exist. A binding that
names no shell command is given an `editor-command.<action>` id, which by
construction is not in the registry and so reports `implemented = false`. The
smoke test asserts the equality directly, so the flag cannot drift.

**Inert.** Every profile also carries `layoutPresetId`, `cameraPresetId`,
`selectionPresetId`, `gridPresetId`, and `terminologyPresetId`. Nothing reads
any of them and changes behavior; they are only printed. The first four are
listed in `unresolvedPresets` on every profile, and because that list is never
empty, `placeholder` is currently `true` for every profile. The reason is
simple: there is no multi-pane level editor to configure yet. The Levels surface
is a single painted 2D viewport (`src/app/map_viewport.*`), not a four-pane
editor with a 3D camera and a component selection model.

**Live.** Selecting a profile now rebinds the shell's keyboard shortcuts.
`StudioCommandRegistry::applyEditorProfile` in `src/app/studio_actions.*` takes
the selected profile's bindings, and for every binding whose `commandId` is a
real shell command and whose `implemented` flag is true, installs that profile's
sequence in place of the documented default from `core/studio_semantics.h`.
Commands the profile does not mention keep their default, so a profile is a
sparse override rather than a full replacement, and switching profiles is not
cumulative: shortcuts are rebuilt from scratch each time. Conflicts are still
resolved in registration order, and a sequence a profile requests that an
earlier command already owns is reported in the diagnostics bundle rather than
silently shadowing its owner.

The shell applies this on start-up and again whenever the preference changes, so
choosing "TrenchBroom-style" in Preferences takes effect without a restart.

**Still inert.** A profile's `mouseGesture` declarations are reported but not
installed: there is no editor interaction layer to route a gesture to yet.

## Target Profiles

Five profiles are registered: the VibeStudio default plus the four below. Each
remaps at least one real shell command and each still declares editor commands
that do not exist yet.

### GtkRadiant 1.6.0-Style
Reference: [GtkRadiant](https://github.com/TTimo/GtkRadiant)

Checklist:
- [ ] Four-pane/orthographic-forward workflow option.
- [ ] Radiant-style camera navigation preset.
- [ ] Radiant-style brush and entity selection defaults.
- [ ] Radiant-style grid stepping and clipping expectations.
- [ ] Radiant-like texture browser placement and terminology.
- [x] Radiant map/project/compiler command conventions declared against real
  shell command ids (`map.open` on `Ctrl+O`, `project.open` on `Ctrl+Shift+O`,
  `compiler.run` on `Ctrl+B`).
- [ ] Common Radiant editor shortcuts routed to editor commands (grid stepping,
  clone, camera) — declared as `editor-command.*` with no resolver.

### NetRadiant Custom-Style
Reference: [NetRadiant Custom](https://github.com/Garux/netradiant-custom)

Checklist:
- [ ] NetRadiant Custom-inspired dense toolbars and filter controls.
- [ ] 3D editing-forward behavior where appropriate.
- [ ] Fast selection/manipulation workflow preset.
- [ ] Texture projection and face-editing shortcuts routed to a real command.
- [x] Compiler/build command conventions aligned with q3map2 workflows
  (`compiler.run`, `build.run-pipeline`, `compiler.copy-cli`).

### TrenchBroom-Style
Reference: [TrenchBroom](https://trenchbroom.github.io/)

Checklist:
- [ ] Single-window, modern brush-editing layout option.
- [ ] TrenchBroom-like camera and selection feel.
- [ ] Face, edge, vertex, and entity workflow routes.
- [ ] Fast texture application and face alignment workflow route.
- [x] Map open/save-as and compiler commands declared against real shell
  command ids.

### QuArK-Style
Reference: [QuArK](https://quark.sourceforge.io/)

Checklist:
- [ ] Explorer/tree-heavy layout option.
- [ ] Multi-document asset/map/package organization.
- [ ] Object/property editing flow inspired by QuArK's integrated model.
- [x] Package and map commands declared side by side in one binding table
  (`map.open`, `package.open`, `package.stage-rename`, `shell.focus-search`).
- [ ] Familiar terminology where it helps users migrate — a terminology preset
  id is stored, but there is no alias table behind it.

## Shared Profile Schema
Profiles should eventually cover:
- [ ] Pane layout and default docks schema — a preset id only; no pane model.
- [x] Keybindings schema with stable command IDs, and a computed flag saying
  whether each id resolves to a shell command.
- [x] Mouse bindings schema with surface scoping (declared; nothing consumes it).
- [ ] Camera movement preset schema — a preset id only.
- [ ] Selection behavior preset schema — a preset id only.
- [ ] Grid defaults schema — a preset id only.
- [ ] Transform gizmo preferences.
- [ ] Texture browser behavior.
- [ ] Entity/property inspector layout.
- [ ] Build/compiler menu layout.
- [ ] Terminology aliases schema — a preset id only; no alias map exists.

## Implementation Rules
- [x] Keep profile settings declarative where practical.
- [x] Declare profile bindings against stable command IDs shared by the shell,
  map, package, and compiler surfaces.
- [ ] Apply a selected profile's shortcut and mouse bindings to the running
  shell.
- [ ] Route every profile action through the final map-editor command stack and
  undo/redo model.
- [x] Add profile-specific tests for binding shape, command-id resolution,
  and shortcut conflicts (`src/tests/editor_profiles_smoke_test.cpp`).
- [ ] Avoid copying third-party icons, artwork, or proprietary assets.
- [x] Credit profile inspiration in README and docs; About surface remains planned.

## Reporting

Both the GUI and the CLI report the honest state rather than the intended one:

- `editorProfileSummaryText()` prefixes each profile with
  `placeholder presets (layout, camera, selection, grid) with N of M bindings
  routed`, and marks every unrouted binding `not implemented yet`.
- `vibestudio --cli editor profiles --json` emits a `placeholder` field per
  profile and an `implemented` flag on every binding. The unresolved preset
  names are reported through `editorProfileSummaryText()` rather than as their
  own JSON field.
- `editorProfileImplementedBindings()` and
  `editorProfileRemappedShellCommandIds()` return only the subset backed by a
  real command, for callers that need the routed set rather than the declared
  one.
