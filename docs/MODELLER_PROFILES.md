# Modeller Profiles And Layout

The Mesh Editor follows the Levels page's layout and borrows its controls from
the modellers people already know. This record covers the layout, the four
controls profiles, how they are stored and customised, and where each profile
stops short of the editor it is named after. The user-facing guide is the
[Models manual page](manual/models.md#choose-your-controls).

**Rip** and **Rip and Fill** are available in every profile's Vertex/Edge menus
and command search. The VibeStudio and Blender profiles bind **V/Alt+V**;
3ds Max and MilkShape retain their existing keys. During a rip, **B** swaps
sides and **F** toggles filling. See [Rip](MODEL_RIP.md) for the common workflow.

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
- the **viewport**, one view or four, under the editing header, with the
  timeline below. The profile's `layout.timeline` flag (**Timeline** in
  **Customise Controls**) shows or hides the timeline;
- a **trailing sidebar** of property pages: **Item**, **Tool**, **Surface**,
  **Animation**, **Skeleton**, **Collision**, **Quake MDL**, **Export**,
  **Health** and **View**.

Above them is one row of menus, **Model** first, and below them one status
strip: the last message, the package and level context and the recovery state,
each on one eliding line with the whole text in its tooltip, **Export
Report…** after an export and, in **Material** shading, the material images'
state.

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
Clicking a pane makes it active, and so do **Next View** and **Previous View**
(Ctrl+Alt+Page Down and Page Up in every profile), which also give it keyboard
focus and announce its name. **Maximise View** fills the viewport with the active
pane and restores the grid again.

| Profile | Default layout | Panes (reading order) |
| --- | --- | --- |
| Studio, Blender | One view | Perspective |
| 3ds Max | Four views | Top, Front, Left, Perspective |
| MilkShape 3D | Four views | Front, Top, Right, Perspective |

Sources disagree on where MilkShape 3D puts its side view; the panes can be
changed on the **View** page.

### Large text and small screens

> [!NOTE]
> **Status: Partial.** Measured offscreen in every profile at 1280x720,
> 1366x768 and 1600x1000 pages, at 100% and 200% text, in the dark and both
> high-contrast themes, left to right and right to left, with strings a third
> longer. Not yet tried with a screen reader or by users of large text.

The editor is shown in a page of the studio, so it must fit that page: once its
minimum is wider or taller than the page, the whole editor, menus included,
scrolls. The layout keeps it inside a 1280x720 page at 200% text
(`model-editor-layout-ui-smoke`):

- **Chrome.** One menu row and one status strip, about three text lines in all.
  The document commands are the **Model** menu's; the menu bar can shrink to
  its overflow button instead of holding every menu in view. Undo, Redo, the
  shading and view preset share the editing header, a tool bar that folds its
  trailing controls into an overflow menu. In **Material** shading the
  material images' state joins the status strip, as a running job does in
  Blender's status bar: one eliding line, the progress bar and **Cancel**
  while images load, and an **Images** menu (**Reload Images**, **Details…**).
  It takes no height from the views and leaves the 3D and UV tabs whole, and
  each of its buttons is a Tab stop, so no action folds out of the keyboard's
  reach. At 200% text in a 1280x720 page it adds about a third of a line to
  the strip (chrome 96 to 109 pixels) and the views keep 53% of the height;
  above the views, as an earlier draft had it, it left them 44%, and in the
  tab bar's corner it hid the tabs and cut its buttons short. Status,
  context, recovery and material lines elide rather than wrap
  (`StatusLineLabel`), so a long message never changes the height of the
  view, which would cancel a drag in it. A line cut short shows its whole
  text in its tooltip and, reached with Tab, when focused.
- **Sidebars.** A page is as wide as what cannot wrap on it, a number field or
  a long word, and at least 18 average characters (16 for the outliner), so the
  floor follows the text scale. Buttons and check boxes wrap their captions
  (`WrappingActionButton`, `WrappingCheckBox`; a check box stretched across a
  row toggles from its indicator and caption only, as Qt's own does, not from
  the blank rest of the row); notes, summaries and a form's
  row labels wrap (a row label keeps the height of its lines,
  `form_row_label_lines.h`, since Qt's form layout would give it one line);
  choices show ten characters at least; forms with labels beside fields move a
  field under its label when both do not fit; and the **Add** page lists
  primitives in one column, so no page scrolls sideways. The fitting runs again
  when the text scale or theme changes.
- **Views.** When the views would keep less than 45% of the editor's width, the
  sidebars first give back what they hold beyond their minimum, then the
  trailing sidebar folds to its tabs and the status strip and screen reader say
  so. A sidebar the user opens stays open: where both sidebars no longer fit,
  or the views would keep less than 45%, the other one folds instead, with the
  same message, and the width it frees goes to the views. Each of the four
  panes may shrink to 160x120 pixels. The tool shelf's icons follow the text
  scale.

Measured in a 1280x720 page: at 200% text with longer strings, right to left,
the editor's minimum went from 1690–1960 by 810–925 pixels to 800–1020 by 555,
the views' share of the width from 15–26% to 46–55% (46% with any property
page open), and the chrome from 7.5 to 2.6 text lines; at 100% text, the
minimum went from up to 1190 by 760 (taller than the page, which scrolled) to
up to 920 by 460, and the chrome from 9.7 to 3.2 lines.

With `VIBESTUDIO_SNAPSHOT_MESH_EDITOR` set, `vibestudio --ui-snapshot <dir>`
also writes `mesh-editor.png`, the studio with the Mesh Editor's tab open, at
the `--ui-snapshot-size` and the text scale and theme of its
`--settings-file`.

Known limits: a choice longer than its box is cut at the box's edge, though its
list shows it whole; at 200% text the trailing sidebar starts folded on pages up
to about 1600 pixels wide, and opening it folds the outliner.

### Dialogs on small screens

Modeller dialogs ask for the size their content reads best at, and
`fitDialogToScreen` (`src/app/dialog_screen_fit.*`) bounds that to the
available area of the screen they open on, less 20 pixels on each side, never
below the dialog's minimum. A dialog whose content can be taller than the
screen (**Customise Controls**, **Animation Curves**, **Edit Animation Keys**)
builds it in a scrolling body (`addScrollingDialogBody`) above its buttons, and
asks for the width that content needs, so it scrolls down rather than
sideways. **Apply**, **OK** and **Cancel** therefore stay on a 1366x768 laptop
screen, or a 1920x1080 screen at 150% scaling (1280x720 logical).
`model-editor-layout-ui-smoke` opens nineteen modeller dialogs at 200% text, in
English and with strings a third longer, on a 1280x720 screen, and checks that
each fits, that its buttons lie inside it, that its body does not scroll
sideways and that no shown label, button or check box is cut short.

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
| Loops, rings of the selected edges | Alt+L, Alt+R | Alt+L, Alt+R | Alt+L, Alt+R | **Select** menu |
| Next, previous view (four views) | Ctrl+Alt+Page Down, Page Up | Ctrl+Alt+Page Down, Page Up | Ctrl+Alt+Page Down, Page Up | Ctrl+Alt+Page Down, Page Up |
| Component modes | 1, 2, 3 | 1, 2, 3 | 1 vertex, 2 edge, 3 border, 4 face, 5 element, 6 object | F5 vertex, F6 face, F8 tags and joints |
| Undo, redo | Ctrl+Z, Ctrl+Shift+Z or Ctrl+Y | Ctrl+Z, Ctrl+Shift+Z | Ctrl+Z, Ctrl+Y | Ctrl+Z, Ctrl+R |
| Knife | K | K | Named command | Named command |
| Texel Density | Named command | Named command | Named command | Named command |

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

Every profile keeps Ctrl+Tab and Ctrl+Shift+Tab (Control+Tab and
Control+Shift+Tab on macOS, where the system keeps Command+Tab) for moving
keyboard focus to the next or previous panel, so a profile that gives Tab to a
command (Blender's edit-mode toggle) never traps focus in the view; such a Tab
key works only while the view has focus. The controls reference and `model controls` list the two
focus keys. **Assign** and **Add** refuse them, and an override or controls file
that binds them is applied without those keys, with a warning (`warnings` in
`model controls --json`). The key field records Tab, Shift+Tab and Esc only
through **Special key**; pressed there, they move focus on or stop recording.
See [Mesh Tools](MODEL_TOOLS.md#keyboard-focus).

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

- **Blender**: no pie menus; Z, Shift+S and
  similar keys open ordinary menus. Ctrl+B invokes [Bevel Edges](MODEL_EDGE_BEVEL.md);
  Ctrl+Shift+B invokes Bevel Vertices. Three-button emulation is off, as in Blender. Arrow keys orbit
  in steps rather than stepping frames.
  **Edge Slide** is available through **Edge**, command search and custom
  bindings; the repeated-G gesture is not emulated. See [edge sliding](MODEL_EDGE_SLIDE.md).
  [Spin](MODEL_SPIN.md) is in the Edge/Face menus, and [Rip](MODEL_RIP.md) uses
  V/Alt+V with whole-operation cancellation and nonzero movement.
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
- **Keyboard additions**: **Next View** and **Previous View** (Ctrl+Alt+Page
  Down and Page Up) are VibeStudio keys in every profile. Alt+L and Alt+R for
  loops and rings of the selected edges are 3ds Max's keys, also given to the
  studio and Blender profiles; MilkShape 3D keeps those commands in the
  **Select** menu.
- **All profiles**: numeric joint/weight authoring is available through
  **Edit Skeleton**; numeric poses and clips use **Edit Skeletal Animation**.
  **Joints** mode and the Skeleton hierarchy share viewport move/rotate/scale
  handles, numeric Geometry controls, axes and pivots. G/R/S select a joint
  handle tool; mesh modal pointer-following and typed-value transforms remain
  separate. [Staged weight painting](MODEL_WEIGHT_PAINT.md) inherits camera controls and adds explicit brush gestures. [Weight transfer](MODEL_WEIGHT_TRANSFER.md) uses the same camera profile for original/result pose comparison. See [joint posing](MODEL_RIG.md#pose-joints-in-the-viewport).
