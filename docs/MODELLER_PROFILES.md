# Modeller Profiles And Layout

The Mesh Editor follows the Levels page's layout and borrows its controls from
the modellers people already know. This record covers the layout, the four
controls profiles, how they are stored and customised, and where each profile
stops short of the editor it is named after. The user-facing guide is the
[Models manual page](manual/models.md#choose-your-controls).

> [!NOTE]
> **Status: Partial.** The profiles, layouts and customisation are available
> and tested offscreen. They have not been tried by long-time users of
> Blender, 3ds Max or MilkShape 3D, and several of those editors' tools have
> no equivalent yet (see [Coverage gaps](#coverage-gaps)).

## Layout

The editor uses the same shell as the level editor, which is itself modelled on
Blender:

- a **leading sidebar** (the left in left-to-right languages) with the
  outliner, a tree of surfaces, frames, tags, collision volumes and joints, and
  the **Add** page of primitives;
- the **viewport**, one view or four, with the material state row above it and
  the timeline below;
- a **trailing sidebar** of property pages: **Item**, **Tool**, **Surface**,
  **Animation**, **Skeleton**, **Collision**, **Quake MDL**, **Export**,
  **Health** and **View**.

Both sidebars use the shared `StudioSidebar` (`src/app/studio_sidebar.*`), so
their tab columns, collapse behaviour, keyboard order and right-to-left
mirroring match the level editor's. Each profile family arranges the pages and
names them the way its editor does (`src/core/model_sidebar.*`):

| Family | Leading pages | Trailing pages (in order) | Names that change |
| --- | --- | --- | --- |
| Studio | Outliner, Add | Item, Tool, Surface, Animation, Skeleton, Collision, Quake MDL, Export, Health, View | None |
| Blender | Outliner, Add | Item, Tool, View, Material, Armature, Animation, Physics, Quake MDL, Export, Mesh Analysis | Surface → Material, Skeleton → Armature, Collision → Physics, Health → Mesh Analysis |
| 3ds Max | Scene Explorer | Create, Modify, Tool Options, Surface Properties, Hierarchy, Motion, Display, Utilities, Physics, Quake MDL | Add → Create, Item → Modify, View → Display, Export → Utilities, Health → xView |
| MilkShape 3D | None | Model, Groups, Materials, Joints, Transform, Animation, Physics, Quake MDL, Export, Health, View, Primitives | Tool → Model, Outliner → Groups, Surface → Materials, Skeleton → Joints, Item → Transform, Add → Primitives |

Which pages are open, their order and the sidebar widths are remembered per
family (`modeller/sidebar/<family>`), so switching profiles and back restores
the arrangement.

### Views

**Layout** on the **View** page chooses one view or four. In four views each
pane can show Perspective, Top, Bottom, Front, Back, Left or Right. One pane is
active at a time: it takes input and has the camera, and the other three
mirror the same mesh, selection and pose from their own fixed angle. Each pane
names its view in its top corner, and the active pane has an accent border.
Clicking a pane makes it active. **Maximise View** fills the viewport with the active
pane and restores the grid again.

| Profile | Default layout | Panes (reading order) |
| --- | --- | --- |
| Studio, Blender | One view | Perspective |
| 3ds Max | Four views | Top, Front, Left, Perspective |
| MilkShape 3D | Four views | Front, Top, Right, Perspective |

Sources disagree on where MilkShape 3D puts its side view; the panes can be
changed on the **View** page.

## Profiles

| Profile | Id | Based on | Transform style |
| --- | --- | --- | --- |
| VibeStudio (Blender-style) | `studio` | Blender, plus Alt+left drag orbit and Ctrl+Y redo | Modal: G, R, S start a transform that follows the pointer |
| Blender | `blender` | Blender 4 default keymap | Modal |
| 3ds Max | `3ds-max` | Autodesk 3ds Max default keyboard shortcuts | Tool: Q, W, E, R pick a tool, then drag |
| MilkShape 3D | `milkshape-3d` | MilkShape 3D 1.8 default shortcuts | Tool: F1–F4 pick a tool, then drag |

Profiles are data (`src/core/model_editor_controls.*`): a profile is a set of
navigation gestures for the 3D and orthographic views, selection gestures, a
transform style, a layout and a key table for about 110 commands. The editor
reads a profile and never branches on its name.

| | Studio | Blender | 3ds Max | MilkShape 3D |
| --- | --- | --- | --- | --- |
| Orbit | Middle drag, Alt+left drag | Middle drag | Alt+middle drag | Left drag (3D view only) |
| Pan | Shift+middle drag | Shift+middle drag | Middle drag | Ctrl+left drag, middle drag |
| Zoom | Ctrl+middle drag, wheel | Ctrl+middle drag, wheel | Ctrl+Alt+middle drag, wheel | Shift+left drag, wheel (inverted in 3D) |
| Orbiting an orthographic view | Turns it into a user view | Turns it into a user view | Turns it into a user view | Never rotates |
| Add to selection | Shift+click (toggles) | Shift+click (toggles) | Ctrl+click | Shift+click |
| Remove from selection | Ctrl+drag | Ctrl+drag | Alt+click | Shift+right drag |
| Edge loop | Alt+click | Alt+click | Double-click an edge | None |
| Component modes | 1, 2, 3 | 1, 2, 3 | 1 vertex, 2 edge, 3 border, 4 face, 5 element, 6 object | F5 vertex, F6 face, F8 tags and joints |
| Undo, redo | Ctrl+Z, Ctrl+Shift+Z or Ctrl+Y | Ctrl+Z, Ctrl+Shift+Z | Ctrl+Z, Ctrl+Y | Ctrl+Z, Ctrl+R |

`vibestudio --cli model controls --profile <id>` prints the full reference for
any profile, and **Show Every Gesture and Key…** on the **View** page shows the
same list in the editor. Each profile's tooltip in **Controls like** lists what
it changes and what it leaves out.

### Choosing a profile

The modeller profile follows the level editor profile until the user picks one
for the modeller: the Blender level profile gives the Blender modeller profile,
and every other level profile gives the studio's own. Choosing a profile on the
**View** page, in **Customise Controls…**, or with
`model controls --select` stores it as `modeller/profileId`. The Models page's
preview viewport uses the same navigation as the editor.

## Customisation

**Customise Controls…** on the **View** page edits the current profile:

- **Keys**: every command with its key sequences. A sequence used twice is
  shown as a problem.
- **Mouse**: orbit, pan and zoom for the 3D and orthographic views, the add and
  remove modifiers, box and lasso selection, edge loop gestures, and
  three-button emulation.
- **Transforms and layout**: modal or tool transforms, fine and snap modifiers,
  layout and panes.

Problems the dialog reports, and `model controls --check` exits 4 on: a key
sequence bound to two commands; two left-click roles on the same modifiers; a
remove gesture equal to the add gesture; a zoom drag that clashes with orbit or
pan; and a pane count that does not match the layout.

Only the differences from the profile's defaults are saved, under
`modeller/controlOverrides/<profile>`. **Reset** drops them. A profile's
changes can be shared as a controls file:

```json
{
    "format": "vibestudio.modeller-controls",
    "version": 1,
    "profile": "3ds-max",
    "overrides": {
        "keys": { "toggleWireframe": ["F3"] },
        "navigation": { "invertWheel3D": true }
    }
}
```

**Export Controls…** and **Import Controls…** on the **View** page read and
write the same file as `model controls --export` and `--import`. A file for a profile
that does not exist, or with an unknown key, is refused with the reason.

## Command Line

| Command | What it does |
| --- | --- |
| `model profiles` | Lists the profiles, their layout, transform style, sidebar family, selection-mode names, what they change and where they differ. The current profile is marked. |
| `model controls` | Prints a profile's controls. `--profile`, `--section navigation|selection|transform|layout|keys`, `--defaults`, `--check`, `--export <file>` with `--overwrite`, `--import <file>`, `--select`, `--reset`, `--settings-file <ini>`. |

```sh
vibestudio --cli model profiles --json
vibestudio --cli model controls --profile 3ds-max --section keys
vibestudio --cli model controls --profile blender --check --json
vibestudio --cli model controls --profile milkshape-3d --export ./milkshape-controls.json
```

## Coverage Gaps

Each profile lists its own gaps in `model profiles`. In summary:

- **Blender**: no knife, spin, edge slide, rip or pie menus; Z, Shift+S and
  similar keys open ordinary menus. Ctrl+B bevels vertices only (use
  Ctrl+Shift+B). Three-button emulation is off, as in Blender. Arrow keys orbit
  in steps rather than stepping frames.
- **3ds Max**: Border (3) selects the open boundary loops of the selection and
  Element (5) whole connected pieces, on the studio's edge and vertex modes.
  Shift+drag with Move duplicates the selection rather than cloning an object.
  Chamfer bevels vertices, Connect runs a loop cut, MeshSmooth subdivides. Cut,
  Quickslice, Swift Loop, soft selection, the modifier stack and quad menus are
  not emulated; right-click opens the context menu.
- **MilkShape 3D**: Groups are surfaces; Joints are tags and skeleton joints.
  F5 and F6 switch to vertex and face selection instead of MilkShape's
  creation tools, and F8 picks tags and joints. Snap Together merges at the
  centre, Divide Edge splits edges, and Set Keyframe copies the pose to the
  current frame. Click-to-create primitives, Delete All and the Comments tab
  are not emulated.
- **All profiles**: there is no bone overlay, pose mode or weight painting,
  so armature and skinning shortcuts have nothing to act on yet.
