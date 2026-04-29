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

### GtkRadiant 1.6.0-Style
Reference: [GtkRadiant](https://github.com/TTimo/GtkRadiant)

Checklist:
- [ ] Four-pane/orthographic-forward workflow option.
- [ ] Radiant-style camera navigation.
- [ ] Radiant-style brush and entity selection defaults.
- [ ] Radiant-style grid stepping and clipping expectations.
- [ ] Radiant-like texture browser placement and terminology.
- [ ] Common Radiant shortcuts mapped by default.

### NetRadiant Custom-Style
Reference: [NetRadiant Custom](https://github.com/Garux/netradiant-custom)

Checklist:
- [ ] NetRadiant Custom-inspired dense toolbars and filter controls.
- [ ] 3D editing-forward behavior where appropriate.
- [ ] Fast selection/manipulation workflow presets.
- [ ] Texture projection and face-editing shortcuts inspired by NetRadiant Custom behavior.
- [ ] Compiler/build menu conventions aligned with q3map2 workflows.

### TrenchBroom-Style
Reference: [TrenchBroom](https://trenchbroom.github.io/)

Checklist:
- [ ] Single-window, modern brush-editing layout option.
- [ ] TrenchBroom-like camera and selection feel.
- [ ] Face, edge, vertex, and entity workflows exposed through compact inspectors.
- [ ] Fast texture application and face alignment workflow.
- [ ] Project/game configuration flow familiar to TrenchBroom users.

### QuArK-Style
Reference: [QuArK](https://quark.sourceforge.io/)

Checklist:
- [ ] Explorer/tree-heavy layout option.
- [ ] Multi-document asset/map/package organization.
- [ ] Object/property editing flow inspired by QuArK's integrated model.
- [ ] Package and asset editing visible alongside map structure.
- [ ] Familiar terminology where it helps users migrate.

## Shared Profile Schema
Profiles should eventually cover:
- [ ] Pane layout and default docks.
- [ ] Keybindings.
- [ ] Mouse bindings.
- [ ] Camera movement.
- [ ] Selection behavior.
- [ ] Grid defaults.
- [ ] Transform gizmo preferences.
- [ ] Texture browser behavior.
- [ ] Entity/property inspector layout.
- [ ] Build/compiler menu layout.
- [ ] Terminology aliases.

## Implementation Rules
- [ ] Keep profile settings declarative where practical.
- [ ] Route all profiles through the same editor command stack and undo/redo model.
- [ ] Add profile-specific tests for keybindings and command routing.
- [ ] Avoid copying third-party icons, artwork, or proprietary assets.
- [ ] Credit profile inspiration in README, docs, and About.
