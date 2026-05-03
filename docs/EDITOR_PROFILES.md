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

## Target Profiles

The MVP registry is active in `src/core/editor_profiles.*` and is shared by the
Qt preferences surface, inspector drawer, CLI, settings storage, and smoke
tests. These profiles now expose routed MVP presets for layout, camera,
selection, grid, terminology, panels, workflow notes, surface-scoped shortcuts,
mouse gestures, and stable command IDs. Full fidelity for every upstream editor
habit remains planned, but the current table is no longer a placeholder-only
registry.

### GtkRadiant 1.6.0-Style
Reference: [GtkRadiant](https://github.com/TTimo/GtkRadiant)

Checklist:
- [x] Four-pane/orthographic-forward workflow option.
- [x] Radiant-style camera navigation preset.
- [x] Radiant-style brush and entity selection defaults.
- [x] Radiant-style grid stepping and clipping expectations.
- [x] Radiant-like texture browser placement and terminology.
- [x] Common Radiant shortcuts routed through the binding table.

### NetRadiant Custom-Style
Reference: [NetRadiant Custom](https://github.com/Garux/netradiant-custom)

Checklist:
- [x] NetRadiant Custom-inspired dense toolbars and filter controls.
- [x] 3D editing-forward behavior where appropriate.
- [x] Fast selection/manipulation workflow preset.
- [x] Texture projection and face-editing shortcuts routed through the binding table.
- [x] Compiler/build menu conventions aligned with q3map2 workflows.

### TrenchBroom-Style
Reference: [TrenchBroom](https://trenchbroom.github.io/)

Checklist:
- [x] Single-window, modern brush-editing layout option.
- [x] TrenchBroom-like camera and selection feel.
- [x] Face, edge, vertex, and entity workflow routes.
- [x] Fast texture application and face alignment workflow route.
- [x] Project/game configuration flow familiar to TrenchBroom users.

### QuArK-Style
Reference: [QuArK](https://quark.sourceforge.io/)

Checklist:
- [x] Explorer/tree-heavy layout option.
- [x] Multi-document asset/map/package organization.
- [x] Object/property editing flow inspired by QuArK's integrated model.
- [x] Package and asset editing visible alongside map structure.
- [x] Familiar terminology where it helps users migrate.

## Shared Profile Schema
Profiles should eventually cover:
- [x] Pane layout and default docks schema.
- [x] Keybindings schema with stable command IDs.
- [x] Mouse bindings schema with surface scoping.
- [x] Camera movement preset schema.
- [x] Selection behavior preset schema.
- [x] Grid defaults schema.
- [ ] Transform gizmo preferences.
- [ ] Texture browser behavior.
- [ ] Entity/property inspector layout.
- [ ] Build/compiler menu layout.
- [x] Terminology aliases schema.

## Implementation Rules
- [x] Keep profile settings declarative where practical.
- [x] Route profile bindings through stable command IDs shared by shell, map, package, and compiler surfaces.
- [ ] Route every profile action through the final map-editor command stack and undo/redo model.
- [x] Add profile-specific tests for keybindings, command routing, and shortcut conflicts.
- [ ] Avoid copying third-party icons, artwork, or proprietary assets.
- [x] Credit profile inspiration in README and docs; About surface remains planned.
