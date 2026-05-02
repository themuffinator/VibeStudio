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
tests. These profiles are intentionally placeholder presets for layout,
camera, selection, grid, terminology, panels, workflow notes, and reserved
bindings; they do not yet switch real map-editor behavior.

### GtkRadiant 1.6.0-Style
Reference: [GtkRadiant](https://github.com/TTimo/GtkRadiant)

Checklist:
- [x] Four-pane/orthographic-forward workflow option placeholder.
- [x] Radiant-style camera navigation preset placeholder.
- [x] Radiant-style brush and entity selection defaults placeholder.
- [x] Radiant-style grid stepping and clipping expectations placeholder.
- [x] Radiant-like texture browser placement and terminology placeholder.
- [x] Common Radiant shortcuts reserved in the placeholder binding table.

### NetRadiant Custom-Style
Reference: [NetRadiant Custom](https://github.com/Garux/netradiant-custom)

Checklist:
- [x] NetRadiant Custom-inspired dense toolbars and filter controls placeholder.
- [x] 3D editing-forward behavior placeholder where appropriate.
- [x] Fast selection/manipulation workflow preset placeholder.
- [x] Texture projection and face-editing shortcuts reserved in the placeholder binding table.
- [x] Compiler/build menu conventions aligned with q3map2 workflows placeholder.

### TrenchBroom-Style
Reference: [TrenchBroom](https://trenchbroom.github.io/)

Checklist:
- [x] Single-window, modern brush-editing layout option placeholder.
- [x] TrenchBroom-like camera and selection feel placeholder.
- [x] Face, edge, vertex, and entity workflow placeholders.
- [x] Fast texture application and face alignment workflow placeholder.
- [x] Project/game configuration flow familiar to TrenchBroom users placeholder.

### QuArK-Style
Reference: [QuArK](https://quark.sourceforge.io/)

Checklist:
- [x] Explorer/tree-heavy layout option placeholder.
- [x] Multi-document asset/map/package organization placeholder.
- [x] Object/property editing flow inspired by QuArK's integrated model placeholder.
- [x] Package and asset editing visible alongside map structure placeholder.
- [x] Familiar terminology where it helps users migrate placeholder.

## Shared Profile Schema
Profiles should eventually cover:
- [x] Pane layout and default docks placeholder schema.
- [x] Keybindings placeholder schema.
- [x] Mouse bindings placeholder schema.
- [x] Camera movement placeholder schema.
- [x] Selection behavior placeholder schema.
- [x] Grid defaults placeholder schema.
- [ ] Transform gizmo preferences.
- [ ] Texture browser behavior.
- [ ] Entity/property inspector layout.
- [ ] Build/compiler menu layout.
- [x] Terminology aliases placeholder schema.

## Implementation Rules
- [x] Keep profile settings declarative where practical.
- [ ] Route all profiles through the same editor command stack and undo/redo model.
- [x] Add profile-specific tests for keybindings and command routing placeholders.
- [ ] Avoid copying third-party icons, artwork, or proprietary assets.
- [x] Credit profile inspiration in README and docs; About surface remains planned.
