# Editor profiles and controls

An editor profile makes the **Levels** page answer the mouse and keyboard like a level editor you already know: which
button pans, how you select, how the camera moves, how the views are laid out and which keys do what. Pick one of 24
profiles, then adjust its gestures and keys to suit you.

> [!NOTE]
> **Status: Partial.** All 24 profiles change the Levels page's gestures, camera, keys and starting layout, and
> automated tests check each one. They are adaptations, not copies of the original editors: native tool modes,
> preference files and some upstream keys are not reproduced, and no profile has been tried with real input devices or
> screen readers yet.

## What a profile changes

- **2D views:** which buttons pan and zoom; how a click selects, adds or toggles; what a drag over empty space does
  (select an area, draw a brush or resize the selection); and, in the Radiant-style profiles, middle-click and arrow-key
  control of the 3D camera.
- **3D camera:** an orthographic view that orbits the map (VibeStudio Default) or a first-person camera with its own
  field of view; the look, orbit and pan drags; what the wheel does; and the keys that fly or drive the camera.
- **Material clicks:** Q3Radiant, GtkRadiant and both NetRadiant profiles sample, paint and paste materials with the
  middle button in the 3D view.
- **Keys:** the keys of Levels commands, such as <kbd>Space</kbd> to duplicate in the Radiant profiles. A profile can
  also take a command's key away when it needs that key for something else: TrenchBroom's <kbd>W</kbd> flies the
  camera, so **3D Wireframe** has no key there.
- **Layout and grid:** how the Levels views are arranged, and the grid size you start with when you switch profile.
- **Sidebars:** which side each Levels sidebar tab starts on, which is open first, and what the tabs are called, such
  as **Entity List** in the Radiant profiles and **Face Edit** in the Hammer ones. Each family of profiles remembers
  the tabs you move; see [Use the sidebars](levels.md#use-the-sidebars). **Browse Editor Profiles…** shows both
  sidebars of the profile you are looking at.

## What a profile does not change

- **Your maps.** Every profile edits the same documents with the same undo, assets and build services. The Hammer
  profile does not add VMF files, and the DarkRadiant profile does not add Doom 3 maps.
- **The rest of the studio.** Other pages, panels and docks stay as they are. The **Models** page and the Mesh Editor
  have their own profiles, modelled on Blender, 3ds Max and MilkShape 3D (see
  [Choose your controls](models.md#choose-your-controls)); they follow the **Blender Style** level profile until
  you choose one there.
- **Native modes and preferences.** Modal tools such as GtkRadiant's edge and vertex dragging, Doom Builder's V, L, S
  and T modes and the gizmos of the scene editors are not reproduced, and preference files from other editors cannot
  be imported. Each profile's block in the [profile reference](#profile-reference) lists its gaps.
- **Your own keys.** Keys you set in **Help** > **Keyboard Shortcuts** always win over the profile's.

## Choose a profile

1. On the **Levels** page, choose **Controls** on the view toolbar. The button shows the current profile, such as
   **VibeStudio** or **TrenchBroom**.
2. Choose **Browse Editor Profiles…** to search by name, aliases such as NRC or TB, or engine family.
3. Review the default layout, gestures, command shortcuts and workflow differences, then choose **Use Profile**.
   Browsing does not change your settings. The **Controls like** menu still offers an immediate selection.

The same choice is **Editor profile** in **Settings** > **Appearance and Language** > **Editing**, and the
**Workspace and Editor Profile** step of [first-run setup](first-run.md) opens it with **Choose Editor Profile**.

| Profile | ID | Starting views | Grid |
| --- | --- | --- | --- |
| VibeStudio Default | `vibestudio-default` | One view, 2D first | 64 units |
| TrenchBroom Style | `trenchbroom` | One view, 3D first | 16 units |
| NetRadiant Custom Style | `netradiant-custom` | 3D camera beside a 2D view | 16 units |
| NetRadiant Style | `netradiant` | 3D camera beside a 2D view | 8 units |
| GtkRadiant 1.6.0 Style | `gtkradiant-1-6` | 3D camera beside a 2D view | 8 units |
| GtkRadiant 1.4 Style | `gtkradiant-1-4` | 3D camera beside a 2D view | 8 units |
| GtkRadiant 1.5 Style | `gtkradiant-1-5` | 3D camera beside a 2D view | 8 units |
| QeRadiant Style | `qeradiant` | 3D camera beside a 2D view | 8 units |
| Q3Radiant Style | `q3radiant` | 3D camera beside a 2D view | 8 units |
| DoomEdit Style | `doomedit` | 3D camera beside a 2D view | 8 units |
| BSP Quake Editor Style | `bsp` | Four views: camera, top, front and side | 16 units |
| DarkRadiant Style | `darkradiant` | 3D camera beside a 2D view | 8 units |
| QuArK Style | `quark` | Four views: camera, top, front and side | 16 units |
| Hammer / Worldcraft Style | `hammer` | Four views: camera, top, front and side | 16 units |
| J.A.C.K. Style | `jack` | Four views: camera, top, front and side | 16 units |
| Sledge Style | `sledge` | Four views: camera, top, front and side | 16 units |
| Doom Builder 2 / X Style | `doom-builder` | One view, 2D first | 32 units |
| Ultimate Doom Builder Style | `ultimate-doom-builder` | One view, 2D first | 32 units |
| SLADE Style | `slade` | One view, 2D first | 32 units |
| Eureka Style | `eureka` | One view, 2D first | 16 units |
| Unreal Editor Style | `unreal` | One view, 3D first | 16 units |
| Unity Scene View Style | `unity` | One view, 3D first | 16 units |
| Godot 3D Style | `godot` | One view, 3D first | 16 units |
| Blender Style | `blender` | One view, 3D first | 16 units |

CLI profile selection also accepts familiar names such as `NRC`, `TB`, `GtkRadiant 1.4.0`,
`GtkRadiant 1.5.0`, `GtkRadiant 1.6.0` and `QE Radiant`. These resolve to the same saved
profile IDs. Quote names containing spaces, for example `editor select "GtkRadiant 1.5.0"`.

## See every gesture and key

Choose **Controls** > **Show Every Gesture and Key...** for a searchable list of everything the Levels views answer to
under your profile, including your own changes. The window can stay open beside your work. Where a profile differs
from the editor it follows, the list starts with **Profile adaptations**. Each Levels view also gives a screen reader
its controls after its own description, and **Help** > **Keyboard Shortcuts** lists the keys of every command.

The **Layout** button next to **Controls** can override the profile's arrangement with **One view, 2D first**,
**One view, 3D first**, **3D camera beside a 2D view**, **Four views: camera, top, front and side** or
**Camera Above Plans** (a wide camera above Top, Front and Side), or **Camera Beside Plans**
(a tall camera beside three stacked plans), keeping the
profile's gestures and keys. **Follow Editor Profile** goes back to the profile's own layout.

## Customise gestures and camera keys

1. Choose **Controls** > **Customize Gestures…**. The same window opens from **Customize Gestures…** under
   **Editor profile** in **Settings**, and from **View** > **Customize Editor Gestures…**. It edits the current profile
   only; each profile keeps its own changes.
2. Change fields on the **2D Plan**, **3D Camera** and **Camera Keys** tabs. **Profile default** follows the built-in
   value. Key fields also offer **Custom key**, where you press the key, and **None**, which turns the key off.
3. Read the status line under the tabs. A conflict, such as two gestures on one button, disables **Apply**. If a
   camera key might take priority over a command's key, a button such as **2 possible shortcut overlaps** lists them.
4. Choose **Apply**. Closing the window without applying drops your changes, and **Restore Defaults** sets every field
   back to the profile's own values, ready to apply.

Movement keys are single keys without <kbd>Ctrl</kbd>, <kbd>Alt</kbd> or <kbd>Shift</kbd>, and one key cannot move two
ways. <kbd>Esc</kbd>, <kbd>Tab</kbd>, modifier keys and lock keys are reserved. **Toggle mouse look** may use modifiers.
**Hold to pan** (2D Plan) and **Hold for mouse look** (Camera Keys) work while you hold the key, and stop when you
release it or the view loses focus.

**Export…** saves your changes to a VibeStudio `.json` file, and **Import…** loads one into the window for you to
apply. A file only imports into the profile it was made for.

## Customise command keys

1. Choose **Help** > **Keyboard Shortcuts**, or **Keyboard Shortcuts…** in **Settings** > **Accessibility**.
2. Type part of a command name, a key or a page in the filter. The **Works on** column shows where each key acts.
3. Select the command, choose **Change Keys…** and press the new keys. The window tells you whether they are free, or
   which command they would be taken from.
4. Choose **Assign**. **No Keys** leaves the command without keys; it still runs from its menu and from command search.

- New keys need <kbd>Ctrl</kbd> or <kbd>Alt</kbd>, or a function key, because plain keys are typed in fields and used
  by the views.
- A few commands the studio relies on keep their keys, and you cannot take keys from them.
- **Reset** gives the selected command its default keys again, and **Reset All** does so for every command you changed.
- The list also shows keys the Levels views handle themselves, such as <kbd>F</kbd> to frame the selection. These
  cannot be changed; where a profile gives the same key to a command, the command wins.

## Behaviour every profile shares

- In a 2D view, a right click opens the map actions menu, a left drag on the selection moves it, and the handles
  resize it. In the 3D view, the labelled X, Y and Z handles resize a Quake-family selection.
- The **Draw Brush**, **Paint** and **Sample** camera tools work the same everywhere; while one is active it takes the
  plain left button in the 3D view. New brushes use the material in the **Material** box above the views.
- Every command stays in the menus and command search, whatever key a profile gives it. A command with no default key,
  such as **Cycle Map View** or **Zoom In**, gets one only from a profile or from you, and that key works only on the
  Levels page.
- The 3D view takes its fly keys only while it has focus; elsewhere those keys keep their commands.
- Losing focus, hiding a view or switching profile ends mouse look and camera flight. A selection dragged in the 3D
  view moves as one undo step, and <kbd>Esc</kbd> drops a move still in progress.
- Reduced motion does not slow mouse look or flight, which follow your hand.

## Use the command line

The `editor` commands read and change the same settings as the studio. Profile IDs and aliases such as `hammer++`,
`udb` or `slade3` both work; settings always store the profile's ID. A change made on the command line applies the
next time the studio loads its settings; it does not control a studio that is already running.

```sh
vibestudio --cli editor profiles
vibestudio --cli editor controls trenchbroom
vibestudio --cli editor select netradiant-custom
vibestudio --cli editor gestures hammer --set camera.flyKeys.forward=I --dry-run --json
```

| Command | What it does |
| --- | --- |
| `editor profiles` | List every profile with its bindings |
| `editor current` | Show the selected profile |
| `editor select <profile>` | Select a profile |
| `editor controls [profile]` | Print a profile's layout, grid, 2D gestures, 3D camera and keys, with your changes |
| `editor gestures [profile]` | Show, change (`--set field=value`), reset (`--reset`), import (`--input`) or export (`--output`) gesture settings |
| `editor layout [preference]` | Show or set the layout: `profile`, `single-2d`, `single-3d`, `camera-and-plan`, `four-views`, `camera-above-plans` or `camera-beside-plans` |
| `editor view-links` | Show or set the linked 2D view and camera-follow options |
| `editor keys` | List the keys you gave commands; `--reset` puts every default back |

Add `--json` for machine-readable output, and `--settings-file <path>` to work on a separate settings file instead of
your own. [Command line](cli.md) covers the rest.

## Profile reference

Each block lists the profile's mouse gestures and the keys that differ from the VibeStudio defaults, which are listed
in [Level editing](levels.md#work-from-the-keyboard). Rows that every profile shares are described under
[Behaviour every profile shares](#behaviour-every-profile-shares). On macOS, <kbd>Ctrl</kbd> means <kbd>Cmd</kbd>, for
keys and clicks alike.

<details>
<summary>VibeStudio Default</summary>

ID `vibestudio-default`. One view, 2D first, 64-unit grid. 3D camera: orthographic, orbiting the map.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Middle drag | Middle drag, <kbd>Shift</kbd>+left drag, <kbd>Ctrl</kbd>+left drag |
| Zoom | Wheel | Wheel |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click adds one | Left click |
| Select by area | Left drag over empty space | — |
| Move the selection | Arrow keys nudge it | — |
| Resize the selection | Left drag its handles | — |
| Orbit | — | Left drag |

| Command | Keys |
| --- | --- |
| Maximize or Restore Active View | <kbd>Ctrl</kbd>+<kbd>Space</kbd> |

Every other command keeps the keys listed in [Level editing](levels.md#work-from-the-keyboard). Not implemented yet: Focus Level View (<kbd>F3</kbd>) and Focus Inspector (<kbd>F8</kbd>).

</details>

<details>
<summary>TrenchBroom Style</summary>

ID `trenchbroom`. One view, 3D first, 16-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Right drag, middle drag | Middle drag |
| Zoom | Wheel | <kbd>Shift</kbd>+wheel zooms the field of view |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click picks a face |
| Select by area | <kbd>Shift</kbd>+left drag | — |
| Draw a brush | Left drag over empty space (<kbd>Shift</kbd> square, <kbd>Ctrl</kbd>+<kbd>Shift</kbd> cube) | — |
| Move the selection | Arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles | — |
| Look around | — | Right drag |
| Orbit | — | <kbd>Alt</kbd>+right drag |
| Move forward and back | — | Wheel |
| Fly | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right, <kbd>Q</kbd> up, <kbd>X</kbd> down; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |

| Command | Keys |
| --- | --- |
| Delete Selection | <kbd>Delete</kbd>, <kbd>Backspace</kbd> |
| Invert Selection | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>A</kbd> |
| Isolate Selection | <kbd>Ctrl</kbd>+<kbd>I</kbd> |
| Hide Selection / Show All Hidden | <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>I</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd> |
| Clip Tool | <kbd>C</kbd> |
| Smaller Grid / Larger Grid | <kbd>-</kbd>, <kbd>[</kbd> / <kbd>+</kbd>, <kbd>=</kbd>, <kbd>]</kbd> |
| Snap to Grid | <kbd>Alt</kbd>+<kbd>0</kbd> |
| Cycle Map View | <kbd>Space</kbd> |
| Frame Selection | <kbd>Ctrl</kbd>+<kbd>U</kbd> |
| Carve | <kbd>Ctrl</kbd>+<kbd>K</kbd> |
| Hollow | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>K</kbd> |
| Show Grid | <kbd>0</kbd> |
| Grid 1 to 256 | <kbd>1</kbd> to <kbd>9</kbd> |

**3D Wireframe** and **Draw Sector** have no key. Not implemented yet: Face Mode (<kbd>F</kbd>) and Vertex Mode (<kbd>V</kbd>). TrenchBroom has no rubber band; this profile adds one on <kbd>Shift</kbd>+drag. <kbd>Ctrl</kbd>+<kbd>F</kbd> stays **Find** instead of flipping objects.

</details>

<details>
<summary>NetRadiant Custom Style</summary>

ID `netradiant-custom`. 3D camera beside a 2D view, 16-unit grid. 3D camera: first-person, 100 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Right drag | Right drag |
| Zoom | Wheel, <kbd>Alt</kbd>+right drag | — |
| Select | Left click; again to pick the next object under the pointer; <kbd>Shift</kbd>+left click adds or removes one | Left click; <kbd>Shift</kbd>+left click adds or removes one; <kbd>Ctrl</kbd>+left click picks a face |
| Select by area | <kbd>Shift</kbd>+left drag | — |
| Draw a brush | Left drag over empty space, nothing selected (<kbd>Shift</kbd> square, <kbd>Ctrl</kbd> cube) | — |
| Move the selection | <kbd>Alt</kbd>+arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles, or over empty space beside it | — |
| Steer the 3D camera | Middle click aims it at the point; <kbd>Ctrl</kbd>+middle click moves it there; arrow keys drive it | — |
| Look around | — | Right click to start or stop, then move the mouse |
| Orbit | — | <kbd>Alt</kbd>+right drag |
| Move forward and back | — | Wheel, toward the pointer |
| Fly while looking | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |
| Drive | — | <kbd>Up</kbd> forward, <kbd>Down</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right, <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right; while looking, the turn keys strafe |
| Material and surface clicks | — | Middle click: sample the material and copy the surface; <kbd>Ctrl</kbd>+middle click: wrap the copied surface onto one face; <kbd>Shift</kbd>+middle click: paste values onto the hit and the selection; <kbd>Shift</kbd>+<kbd>Alt</kbd>+middle click: paste mapping values only; <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+middle click: wrap mapping only; <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+middle click: project the copied surface onto the hit and the selection; <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Alt</kbd>+middle click: project mapping only |

| Command | Keys |
| --- | --- |
| Duplicate Selection | <kbd>Space</kbd>, <kbd>Shift</kbd>+<kbd>Space</kbd> |
| Delete Selection | <kbd>Delete</kbd>, <kbd>Backspace</kbd>, <kbd>Z</kbd> |
| Select None | <kbd>C</kbd> |
| Invert Selection | <kbd>I</kbd> |
| Next 2D View | <kbd>Ctrl</kbd>+<kbd>Tab</kbd> |
| Top View / Front View / Side View | <kbd>Keypad 7</kbd> / <kbd>Keypad 1</kbd> / <kbd>Keypad 3</kbd> |
| 3D View | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> |
| Maximize or Restore Active View | <kbd>F12</kbd> |
| Frame Selection | <kbd>&#96;</kbd> |
| Carve | <kbd>Shift</kbd>+<kbd>U</kbd> |
| Snap Selection to Grid | <kbd>Ctrl</kbd>+<kbd>G</kbd> |
| Connect Entities | <kbd>Ctrl</kbd>+<kbd>K</kbd> |
| Show Grid | <kbd>0</kbd> |
| Grid 1 to 256 | <kbd>1</kbd> to <kbd>9</kbd> |

**3D Wireframe** has no key. Not implemented yet: Fit Texture (<kbd>Ctrl</kbd>+<kbd>F</kbd>) and Expand Selection (<kbd>Shift</kbd>+<kbd>E</kbd>). Copying or wrapping from a patch, and sampling depth or light colour, are not supported.

</details>

<details>
<summary>NetRadiant Style</summary>

ID `netradiant`, also `net-radiant`, `xonotic-netradiant`, `netradiant-classic`. 3D camera beside a 2D view, 8-unit grid. 3D camera: first-person, 110 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Right drag | — |
| Zoom | Wheel, <kbd>Alt</kbd>+right drag | — |
| Select | Left click; again to pick the next object under the pointer; <kbd>Shift</kbd>+left click adds or removes one | Left click; <kbd>Shift</kbd>+left click adds or removes one; <kbd>Ctrl</kbd>+left click picks a face |
| Select by area | <kbd>Shift</kbd>+left drag | — |
| Draw a brush | Left drag over empty space, nothing selected (<kbd>Shift</kbd> square, <kbd>Ctrl</kbd> cube) | — |
| Move the selection | <kbd>Alt</kbd>+arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles, or over empty space beside it | — |
| Steer the 3D camera | Middle click aims it at the point; <kbd>Ctrl</kbd>+middle click moves it there; arrow keys drive it | — |
| Look around | — | Right click to start or stop, then move the mouse |
| Move forward and back | — | Wheel, toward the pointer |
| Fly while looking | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |
| Drive | — | <kbd>Up</kbd> forward, <kbd>Down</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right, <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right; while looking, the turn keys strafe |
| Material and surface clicks | — | Middle click: sample the material and copy the surface; <kbd>Shift</kbd>+middle click: paste the copied surface on one face |

| Command | Keys |
| --- | --- |
| Duplicate Selection | <kbd>Space</kbd> |
| Delete Selection | <kbd>Backspace</kbd>, <kbd>Z</kbd> |
| Select None | <kbd>Esc</kbd>, <kbd>C</kbd> |
| Invert Selection | <kbd>I</kbd> |
| Next 2D View | <kbd>Ctrl</kbd>+<kbd>Tab</kbd> |
| Top View / Front View / Side View | <kbd>Keypad 7</kbd> / <kbd>Keypad 1</kbd> / <kbd>Keypad 3</kbd> |
| 3D View | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> |
| Maximize or Restore Active View | <kbd>F12</kbd> |
| Frame Selection | <kbd>&#96;</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> |
| Carve | <kbd>Shift</kbd>+<kbd>U</kbd> |
| Snap Selection to Grid | <kbd>Ctrl</kbd>+<kbd>G</kbd> |
| Connect Entities | <kbd>Ctrl</kbd>+<kbd>K</kbd> |
| Redo Map Edit | <kbd>Ctrl</kbd>+<kbd>Y</kbd> |
| Show Grid | <kbd>0</kbd> |
| Select All of This Class | <kbd>Shift</kbd>+<kbd>A</kbd> |
| Zoom In / Zoom Out | <kbd>Delete</kbd> / <kbd>Insert</kbd> |
| Grid 1 to 256 | <kbd>1</kbd> to <kbd>9</kbd> |

**Select All** and **3D Wireframe** have no key. Tab keeps moving keyboard focus instead of focusing the camera. Discrete camera steps, floor stepping, right-button selection painting and unique-target cloning are not reproduced. The upstream <kbd>Ctrl</kbd>+middle and <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+middle paste chords are unbound; choose one in **Customize Gestures…** if you want them.

</details>

<details>
<summary>GtkRadiant 1.6.0 Style</summary>

ID `gtkradiant-1-6`. 3D camera beside a 2D view, 8-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Right drag | — |
| Zoom | Wheel, <kbd>Shift</kbd>+right drag | — |
| Select | <kbd>Shift</kbd>+left click selects or deselects; a plain click selects nothing; <kbd>Shift</kbd>+<kbd>Alt</kbd>+left click picks the next object under the pointer | <kbd>Shift</kbd>+left click selects or deselects; a plain click selects nothing; <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+left click picks a face |
| Select by area | <kbd>Alt</kbd>+left drag | — |
| Draw a brush | Left drag over empty space, nothing selected | — |
| Move the selection | <kbd>Alt</kbd>+arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles, or over empty space beside it | — |
| Steer the 3D camera | Middle click aims it at the point; <kbd>Ctrl</kbd>+middle click moves it there; arrow keys drive it | — |
| Look around | — | Right click to start or stop, then move the mouse |
| Move forward and back | — | Wheel |
| Fly while looking | — | <kbd>Up</kbd> forward, <kbd>Down</kbd> back, <kbd>,</kbd> left, <kbd>.</kbd> right, <kbd>D</kbd> up, <kbd>C</kbd> down, <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |
| Drive | — | <kbd>Up</kbd> forward, <kbd>Down</kbd> back, <kbd>,</kbd> left, <kbd>.</kbd> right, <kbd>D</kbd> up, <kbd>C</kbd> down, <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right; while looking, the turn keys strafe |
| Material and surface clicks | — | Middle click: sample the material and copy the surface; <kbd>Shift</kbd>+middle click: paint the material on one surface; <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+middle click: paste the copied surface on one face; <kbd>Ctrl</kbd>+middle click: paste it on the whole brush |

| Command | Keys |
| --- | --- |
| Duplicate Selection | <kbd>Space</kbd> |
| Delete Selection | <kbd>Backspace</kbd> |
| Zoom In / Zoom Out | <kbd>Delete</kbd> / <kbd>Insert</kbd> |
| Select All of This Class | <kbd>Shift</kbd>+<kbd>A</kbd> |
| Invert Selection | <kbd>I</kbd> |
| Next 2D View | <kbd>Ctrl</kbd>+<kbd>Tab</kbd> |
| Frame Selection | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> |
| 3D View | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> |
| Carve | <kbd>Shift</kbd>+<kbd>U</kbd> |
| Snap Selection to Grid | <kbd>Ctrl</kbd>+<kbd>G</kbd> |
| Connect Entities | <kbd>Ctrl</kbd>+<kbd>K</kbd> |
| Load Leak Trail | <kbd>Shift</kbd>+<kbd>L</kbd> |
| Redo Map Edit | <kbd>Ctrl</kbd>+<kbd>Y</kbd> |
| Show Grid | <kbd>0</kbd> |
| Grid 1 to 256 | <kbd>1</kbd> to <kbd>9</kbd> |

**3D Wireframe** has no key. Not implemented yet: Drag Edges (<kbd>E</kbd>), Drag Vertices (<kbd>V</kbd>), Mouse Rotate (<kbd>R</kbd>) and Make Detail (<kbd>Ctrl</kbd>+<kbd>M</kbd>). The camera's <kbd>A</kbd> and <kbd>Z</kbd> pitch keys are not bound. Pasting a surface onto every selected object, and sampling brush depth or light colour, are not supported.

</details>

<details>
<summary>GtkRadiant 1.4 and 1.5 Styles</summary>

IDs `gtkradiant-1-4` and `gtkradiant-1-5`, also `gtk14` and `gtk15`. Both start with a
90-degree camera beside a plan and an 8-unit grid. They follow the 1.4.0-era ZeroRadiant
and GtkRadiant 1.5 sources; the original editor's complete toolset is not reproduced.

| Action | GtkRadiant 1.4 | GtkRadiant 1.5 |
| --- | --- | --- |
| Select | <kbd>Shift</kbd>+left click; <kbd>Shift</kbd>+<kbd>Alt</kbd> cycles stacked objects | Same |
| Select by area | <kbd>Alt</kbd>+left drag | <kbd>Shift</kbd>+left drag |
| Plan pan / zoom | Right drag / <kbd>Shift</kbd>+right drag or wheel | Same |
| Camera look | Right click starts/stops free look | Same |
| Camera steps outside free look | Arrows move/turn, comma/period strafe, <kbd>D</kbd>/<kbd>C</kbd> rise/sink, <kbd>A</kbd>/<kbd>Z</kbd> pitch up/down | Same |
| Camera flight during free look | Arrows move/strafe; comma/period, <kbd>D</kbd>/<kbd>C</kbd> also move | Arrow-key translation |
| Sample a surface | Middle click | Middle click |
| Paint material only | <kbd>Shift</kbd>+middle click | Unassigned |
| Paste surface on a brush / face | <kbd>Ctrl</kbd>+middle / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+middle | Face paste only: <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+middle |

Camera steps outside free look move 32 units or turn 22.5 degrees. Modified arrows remain
available to texture commands there. Both profiles clone with <kbd>Space</kbd>, delete with
<kbd>Backspace</kbd>, zoom the plan with <kbd>Delete</kbd>/<kbd>Insert</kbd>, merge brushes with
<kbd>Ctrl</kbd>+<kbd>U</kbd>, fit textures with <kbd>Shift</kbd>+<kbd>B</kbd> and frame selection with
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd>. <kbd>S</kbd> opens Surface Alignment,
<kbd>Shift</kbd>+<kbd>S</kbd> edits patches, <kbd>Shift</kbd>+<kbd>C</kbd> caps patches, and
<kbd>Shift</kbd>+<kbd>T</kbd> toggles texture lock. Shift+arrows shift texture U/V;
Shift+Page Up/Down rotate using the current Surfaces target and step settings.

1.5 area selection adds members instead of toggling them, and surface paste uses the hit face.
Native replacement-area selection, component manipulators, fractional grids, floor stepping,
Z-checker and preference imports are not reproduced. The 1.4 preset keeps <kbd>Shift</kbd>+<kbd>L</kbd>
for Load Leak Trail; 1.5 leaves it unassigned. Neither preset adds Doom 3 map support.

</details>

<details>
<summary>QeRadiant Style</summary>

ID `qeradiant`, also `qe-radiant`, `qer`, `qe` or `quake2-radiant`. The classic Quake II
profile uses the camera, plan, Shift selection, Space cloning and Backspace deletion described
under Q3Radiant below. Right drag steers the camera; <kbd>Ctrl</kbd>+right drag pans it.
Texture fitting uses <kbd>Shift</kbd>+<kbd>5</kbd> or <kbd>Ctrl</kbd>+<kbd>F</kbd> while a map view
has focus, using the Surfaces target and current steps.

Q3-specific patch, hide/show, select-similar and texture-lock keys are unassigned. Those
VibeStudio commands remain available in menus and command search. Whole-entity selection mode,
animated entity previews, Alt+right texture dragging and Z-checker are not reproduced.
Middle-button sampling and surface paste follow the shared studio clipboard rules; sampling
does not automatically repaint the selected objects.

</details>

<details>
<summary>Q3Radiant Style</summary>

ID `q3radiant`, also `q3-radiant`, `quake3-radiant`, `quake-iii-radiant`. 3D camera beside a 2D view, 8-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Right drag | <kbd>Ctrl</kbd>+right drag |
| Zoom | Wheel | — |
| Select | <kbd>Shift</kbd>+left click selects or deselects; a plain click selects nothing; <kbd>Shift</kbd>+<kbd>Alt</kbd>+left click picks the next object under the pointer | <kbd>Shift</kbd>+left click selects or deselects; a plain click selects nothing; <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+left click picks a face |
| Select by area | <kbd>Alt</kbd>+left drag | — |
| Draw a brush | Left drag over empty space, nothing selected | — |
| Move the selection | <kbd>Alt</kbd>+arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles, or over empty space beside it | — |
| Steer the 3D camera | Middle click aims it at the point; <kbd>Ctrl</kbd>+middle click moves it there; arrow keys drive it in fixed steps of 32 units or 22.5 degrees | — |
| Steer | — | Hold the right button: above or below the centre moves forward or back, left or right turns; the centre stops, and releasing or <kbd>Esc</kbd> ends it |
| Move forward and back | — | Wheel |
| Drive | — | <kbd>Up</kbd> forward, <kbd>Down</kbd> back, <kbd>,</kbd> left, <kbd>.</kbd> right, <kbd>D</kbd> up, <kbd>C</kbd> down, <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right, <kbd>A</kbd> look up, <kbd>Z</kbd> look down; each press steps 32 units or 22.5 degrees on the ground plane |
| Material and surface clicks | — | Middle click: sample the material and copy the surface; <kbd>Shift</kbd>+middle click: paint the material on one surface; <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+middle click: paste the copied surface on one face; <kbd>Ctrl</kbd>+middle click: paste it on the whole brush |

| Command | Keys |
| --- | --- |
| Duplicate Selection | <kbd>Space</kbd> |
| Delete Selection | <kbd>Backspace</kbd> |
| Zoom In / Zoom Out | <kbd>Delete</kbd> / <kbd>Insert</kbd> |
| Select All of This Class | <kbd>Shift</kbd>+<kbd>A</kbd> |
| Next 2D View | <kbd>Ctrl</kbd>+<kbd>Tab</kbd> |
| 3D View | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> |
| Carve | <kbd>Shift</kbd>+<kbd>U</kbd> |
| Snap Selection to Grid | <kbd>Ctrl</kbd>+<kbd>G</kbd> |
| Connect Entities | <kbd>Ctrl</kbd>+<kbd>K</kbd> |
| Redo Map Edit | <kbd>Ctrl</kbd>+<kbd>Y</kbd> |
| Show Grid | <kbd>0</kbd> |
| Select None | <kbd>Esc</kbd> |
| Merge Brushes | <kbd>Ctrl</kbd>+<kbd>U</kbd> |
| Surface Alignment | <kbd>S</kbd> |
| Edit Patch Control Points | <kbd>Shift</kbd>+<kbd>S</kbd> |
| Cap Patch | <kbd>Shift</kbd>+<kbd>C</kbd> |
| Texture Lock | <kbd>Shift</kbd>+<kbd>T</kbd> |
| Shift Texture U − / Shift Texture U + | <kbd>Shift</kbd>+<kbd>Left</kbd> / <kbd>Shift</kbd>+<kbd>Right</kbd> |
| Shift Texture V − / Shift Texture V + | <kbd>Shift</kbd>+<kbd>Down</kbd> / <kbd>Shift</kbd>+<kbd>Up</kbd> |
| Rotate Texture − / Rotate Texture + | <kbd>Shift</kbd>+<kbd>Page Up</kbd> / <kbd>Shift</kbd>+<kbd>Page Down</kbd> |
| Fit Texture 1 × 1 | <kbd>Shift</kbd>+<kbd>5</kbd> |
| Grid 1 to 256 | <kbd>1</kbd> to <kbd>9</kbd> |

**Invert Selection**, **Frame Selection**, **Load Leak Trail**, **3D Wireframe** and **Select All** have no key. Texture shifts use the step values on the **Surfaces** tab, not Q3Radiant's camera-relative shifts, and <kbd>Ctrl</kbd>+arrow scaling is unbound. Native preferences, the Z-checker, floor stepping, terrain, and bend and rotation modes are not reproduced.

</details>

<details>
<summary>DarkRadiant Style</summary>

ID `darkradiant`, also `dark-radiant`. 3D camera beside a 2D view, 8-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Right drag | — |
| Zoom | Wheel, <kbd>Shift</kbd>+right drag | — |
| Select | <kbd>Shift</kbd>+left click selects or deselects; a plain click selects nothing; <kbd>Shift</kbd>+<kbd>Alt</kbd>+left click picks the next object under the pointer | <kbd>Shift</kbd>+left click selects or deselects; a plain click selects nothing; <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+left click picks a face |
| Select by area | <kbd>Alt</kbd>+left drag | — |
| Draw a brush | Left drag over empty space, nothing selected | — |
| Move the selection | <kbd>Alt</kbd>+arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles, or over empty space beside it | — |
| Steer the 3D camera | Middle click aims it at the point; <kbd>Ctrl</kbd>+middle click moves it there; arrow keys drive it | — |
| Look around | — | Right click to start or stop, then move the mouse |
| Move forward and back | — | Wheel |
| Fly while looking | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |
| Drive | — | <kbd>Up</kbd> forward, <kbd>Down</kbd> back, <kbd>,</kbd> left, <kbd>.</kbd> right, <kbd>D</kbd> up, <kbd>C</kbd> down, <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right; while looking, the turn keys strafe |

| Command | Keys |
| --- | --- |
| Duplicate Selection | <kbd>Space</kbd> |
| Delete Selection | <kbd>Backspace</kbd> |
| Zoom In / Zoom Out | <kbd>Delete</kbd> / <kbd>Insert</kbd> |
| Select All of This Class | <kbd>Shift</kbd>+<kbd>A</kbd> |
| Invert Selection | <kbd>I</kbd> |
| Next 2D View | <kbd>Ctrl</kbd>+<kbd>Tab</kbd> |
| Frame Selection | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> |
| 3D View | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> |
| Carve | <kbd>Shift</kbd>+<kbd>U</kbd> |
| Snap Selection to Grid | <kbd>Ctrl</kbd>+<kbd>G</kbd> |
| Connect Entities | <kbd>Ctrl</kbd>+<kbd>K</kbd> |
| Load Leak Trail | <kbd>Shift</kbd>+<kbd>L</kbd> |
| Redo Map Edit | <kbd>Ctrl</kbd>+<kbd>Y</kbd> |
| Show Grid | <kbd>0</kbd> |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Grid 1 to 256 | <kbd>1</kbd> to <kbd>9</kbd> |

**3D Wireframe** has no key. Choosing it does not add Doom 3 or Quake 4 formats, fractional grid steps or a Dark Mod game connection.

</details>

<details>
<summary>QuArK Style</summary>

ID `quark`, also `quake-army-knife`. Four views: camera, top, front and side, 16-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Right drag | Middle drag |
| Zoom | Wheel, middle drag | — |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click adds one | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click picks a face |
| Select by area | Left drag over empty space | — |
| Move the selection | Arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles | — |
| Look around | — | Right drag |
| Move forward and back | — | Wheel |
| Drive | — | <kbd>Up</kbd> forward, <kbd>Down</kbd> back, <kbd>End</kbd> left, <kbd>Page Down</kbd> right, <kbd>D</kbd> up, <kbd>C</kbd> down, <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right; while looking, the turn keys strafe |

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Select None | <kbd>Esc</kbd> |

**3D Wireframe** and **Draw Sector** have no key. Not implemented yet: Object Properties (<kbd>Alt</kbd>+<kbd>Enter</kbd>), Focus Object Tree (<kbd>F6</kbd>) and Rename Object (<kbd>F2</kbd>). Four synchronised views replace QuArK's window layout. Its extra selection keys, tree hotkeys, multi-button gestures and project files are not reproduced.

</details>

<details>
<summary>Hammer / Worldcraft Style</summary>

ID `hammer`, also `worldcraft`, `valve-hammer`, `hammer++`, `hammer-plus-plus`. Four views: camera, top, front and side, 16-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Middle drag | Right drag |
| Zoom | Wheel | — |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click adds one | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click picks a face |
| Select by area | Left drag over empty space | — |
| Move the selection | Arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles | — |
| Look around | — | Middle drag; <kbd>Z</kbd> turns mouse look on or off |
| Move forward and back | — | Wheel |
| Fly while looking | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Select None | <kbd>Shift</kbd>+<kbd>Q</kbd> |
| Clip Tool | <kbd>Shift</kbd>+<kbd>X</kbd> |
| Carve | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> |
| Hollow | <kbd>Ctrl</kbd>+<kbd>H</kbd> |
| Snap Selection to Grid | <kbd>Ctrl</kbd>+<kbd>B</kbd> |
| Flip Vertical | <kbd>Ctrl</kbd>+<kbd>I</kbd> |
| Flip Horizontal | <kbd>Ctrl</kbd>+<kbd>L</kbd> |
| Texture Lock | <kbd>Shift</kbd>+<kbd>L</kbd> |
| Frame Selection | <kbd>Ctrl</kbd>+<kbd>E</kbd> |
| Show Grid | <kbd>Shift</kbd>+<kbd>R</kbd> |
| Snap to Grid | <kbd>Shift</kbd>+<kbd>W</kbd> |
| Next 2D View | <kbd>Tab</kbd> |
| Maximize or Restore Active View | <kbd>Shift</kbd>+<kbd>Z</kbd> |
| Build and Launch | <kbd>F9</kbd> |
| Equalize View Sizes | <kbd>Ctrl</kbd>+<kbd>A</kbd> |

**Duplicate Selection**, **Select All**, **3D Wireframe** and **Draw Sector** have no key. Use **Edit** > **Duplicate Selection** and **Edit** > **Select All Map Objects** instead. Space-drag, duplicate-on-drag, displacements, Source 2 modes and VMF or RMF files are not supported.

</details>

<details>
<summary>J.A.C.K. Style</summary>

ID `jack`, also `j.a.c.k.`, `jackhammer`. Four views: camera, top, front and side, 16-unit grid. 3D camera: first-person, 90 degree field of view.

Mouse gestures and camera keys are the same as **Hammer / Worldcraft Style**.

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Select None | <kbd>Ctrl</kbd>+<kbd>Q</kbd>, <kbd>Shift</kbd>+<kbd>Q</kbd> |
| Clip Tool | <kbd>Shift</kbd>+<kbd>X</kbd> |
| Carve | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> |
| Hollow | <kbd>Ctrl</kbd>+<kbd>U</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>H</kbd> |
| Snap Selection to Grid | <kbd>Ctrl</kbd>+<kbd>B</kbd> |
| Flip Vertical | <kbd>Ctrl</kbd>+<kbd>I</kbd> |
| Flip Horizontal | <kbd>Ctrl</kbd>+<kbd>L</kbd> |
| Texture Lock | <kbd>Shift</kbd>+<kbd>L</kbd> |
| Frame Selection | <kbd>Ctrl</kbd>+<kbd>E</kbd> |
| Show Grid | <kbd>Shift</kbd>+<kbd>R</kbd> |
| Snap to Grid | <kbd>Shift</kbd>+<kbd>W</kbd> |
| Next 2D View | <kbd>Tab</kbd> |
| Maximize or Restore Active View | <kbd>Shift</kbd>+<kbd>Z</kbd> |
| Build and Launch | <kbd>F9</kbd> |
| Isolate Selection | <kbd>Ctrl</kbd>+<kbd>H</kbd> |
| Invert Selection | <kbd>Shift</kbd>+<kbd>I</kbd> |
| Show All Hidden | <kbd>U</kbd> |
| Rotate 90 Degrees Left / Rotate 90 Degrees Right | <kbd>Ctrl</kbd>+<kbd>R</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>R</kbd> |

**Duplicate Selection**, **3D Wireframe** and **Draw Sector** have no key. JMF files, detail and structural modes, texture-tool function keys and texture application gestures are not reproduced.

</details>

<details>
<summary>Sledge Style</summary>

ID `sledge`, also `sledge-editor`, `sledge2`, `sledge-2`. Four views: camera, top, front and side, 16-unit grid. 3D camera: first-person, 60 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Middle drag; hold <kbd>Space</kbd> and move the pointer; <kbd>Shift</kbd>+arrow keys pan a quarter of the view | — |
| Zoom | Wheel | — |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click adds one | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click picks a face |
| Select by area | Left drag over empty space | — |
| Move the selection | Arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles | — |
| Look around | — | <kbd>Z</kbd> turns mouse look on or off; hold <kbd>Space</kbd> to look until you let go; while looking, the right button pans and both buttons move along the view |
| Move forward and back | — | Wheel |
| Fly | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right, <kbd>Q</kbd> up, <kbd>E</kbd> down; <kbd>Shift</kbd> faster, <kbd>Ctrl</kbd> slower (the speed keys work only while looking) |
| Drive | — | <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right, <kbd>Up</kbd> look up, <kbd>Down</kbd> look down; <kbd>Shift</kbd>+turn keys strafe and <kbd>Shift</kbd>+look keys move up or down |

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Select None | <kbd>Shift</kbd>+<kbd>Q</kbd> |
| Clip Tool | <kbd>Shift</kbd>+<kbd>X</kbd> |
| Carve | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>C</kbd> |
| Hollow | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>H</kbd> |
| Isolate Selection | <kbd>Ctrl</kbd>+<kbd>H</kbd> |
| Show All Hidden | <kbd>U</kbd> |
| Texture Lock | <kbd>Shift</kbd>+<kbd>L</kbd> |
| Show Grid | <kbd>Shift</kbd>+<kbd>R</kbd> |
| Snap to Grid | <kbd>Shift</kbd>+<kbd>W</kbd> |
| Maximize or Restore Active View | <kbd>Shift</kbd>+<kbd>Z</kbd> |
| Build and Launch | <kbd>F9</kbd> |

**Duplicate Selection**, **3D Wireframe** and **Draw Sector** have no key. Navigation speeds, zoom presets, linked wheel zoom, duplicate-on-drag, grouping and tool modes differ from Sledge, and its preferences and RMF or VMF files are not imported.

</details>

<details>
<summary>Doom Builder 2 / X Style</summary>

ID `doom-builder`, also `doom-builder-2`, `doombuilder`, `doombuilder2`, `doom-builder-x`, `db2`, `dbx`. One view, 2D first, 32-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Middle drag | Middle drag |
| Zoom | Wheel | — |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click adds one | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click picks a face |
| Select by area | Left drag over empty space | — |
| Move the selection | Arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles | — |
| Look around | — | Right drag |
| Move forward and back | — | Wheel |
| Fly | — | <kbd>E</kbd> forward, <kbd>D</kbd> back, <kbd>S</kbd> left, <kbd>F</kbd> right; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Select None | <kbd>C</kbd> |
| Draw Sector | <kbd>Ctrl</kbd>+<kbd>D</kbd> |
| 3D View | <kbd>W</kbd> |
| Build and Launch | <kbd>F9</kbd> |

**Duplicate Selection**, **3D Wireframe** and **Clip Tool** have no key. Crosshair editing, right-drag movement, gravity, the V, L, S and T modes and wheel height editing are not reproduced; painting and sampling are separate tools.

</details>

<details>
<summary>Ultimate Doom Builder Style</summary>

ID `ultimate-doom-builder`, also `udb`, `gzdoom-builder`, `gzdb`, `gzdoom-builder-bugfix`. One view, 2D first, 32-unit grid. 3D camera: first-person, 90 degree field of view.

Mouse gestures and camera keys are the same as **Doom Builder 2 / X Style**.

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Select None | <kbd>C</kbd> |
| Draw Sector | <kbd>Ctrl</kbd>+<kbd>D</kbd> |
| 3D View | <kbd>Q</kbd> |
| Build and Launch | <kbd>F9</kbd> |

**Duplicate Selection**, **3D Wireframe** and **Clip Tool** have no key. Slopes, 3D floors, gravity and source-port visual effects are not shown.

</details>

<details>
<summary>SLADE Style</summary>

ID `slade`, also `slade3`, `slade-3`. One view, 2D first, 32-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Right drag | Middle drag |
| Zoom | Wheel | — |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click adds one | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click picks a face |
| Select by area | Left drag over empty space | — |
| Move the selection | Arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles | — |
| Look around | — | Right drag |
| Move forward and back | — | Wheel |
| Fly | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right, <kbd>Up</kbd> up, <kbd>Down</kbd> down; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |
| Drive | — | <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right; while looking, the turn keys strafe |

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Select None | <kbd>C</kbd> |
| 3D View | <kbd>Q</kbd> |
| Snap to Grid | <kbd>Shift</kbd>+<kbd>G</kbd> |
| Zoom In / Zoom Out | <kbd>=</kbd> / <kbd>-</kbd> |

**3D Wireframe**, **Draw Sector** and **Clip Tool** have no key. Painting and sampling are separate tools. The V, L, S and T modes, visual-mode texture gestures and wheel height changes are not reproduced.

</details>

<details>
<summary>Eureka Style</summary>

ID `eureka`, also `eureka-doom`. One view, 2D first, 16-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Middle drag | Middle drag |
| Zoom | Wheel | — |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click adds one | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click picks a face |
| Select by area | Left drag over empty space | — |
| Move the selection | Arrow keys nudge it | Left drag it; with <kbd>Alt</kbd>, up and down |
| Resize the selection | Left drag its handles | — |
| Look around | — | Right drag |
| Move forward and back | — | Wheel |
| Fly | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right, <kbd>Page Up</kbd> up, <kbd>Page Down</kbd> down; <kbd>Alt</kbd> faster, <kbd>Shift</kbd> slower |
| Drive | — | <kbd>Up</kbd> forward, <kbd>Down</kbd> back, <kbd>Left</kbd> turn left, <kbd>Right</kbd> turn right; while looking, the turn keys strafe |

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Duplicate Selection | <kbd>O</kbd> |
| 3D View | <kbd>Tab</kbd> |
| Zoom In / Zoom Out | <kbd>=</kbd> / <kbd>-</kbd> |
| Grid 2 to 256 | <kbd>1</kbd> to <kbd>8</kbd> |

**3D Wireframe** and **Draw Sector** have no key. Grid digits cover 2 to 256 units; 512 and 1024, letter-held scrolling, right-button line drawing and the V, L, S and T modes are not implemented.

</details>

<details>
<summary>Unreal Editor Style</summary>

ID `unreal`, also `unreal-editor`, `ue4`, `ue5`. One view, 3D first, 16-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Middle drag, right drag | Middle drag |
| Zoom | Wheel | — |
| Select | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click adds one | Left click; <kbd>Ctrl</kbd>+left click adds or removes one; <kbd>Shift</kbd>+left click picks a face |
| Select by area | Left drag over empty space | — |
| Move the selection | Arrow keys nudge it | — |
| Resize the selection | Left drag its handles | — |
| Look around | — | Right drag |
| Orbit | — | <kbd>Alt</kbd>+left drag |
| Move forward and back | — | Wheel |
| Fly while looking | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right, <kbd>E</kbd> up, <kbd>Q</kbd> down; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Duplicate Selection | <kbd>Ctrl</kbd>+<kbd>W</kbd> |
| Frame Selection | <kbd>F</kbd> |
| Show All Hidden | <kbd>Ctrl</kbd>+<kbd>H</kbd> |

**3D Wireframe** and **Draw Sector** have no key. Moving and resizing use VibeStudio's handles and numeric tools. Gizmo modes, left-button camera driving, wheel flight-speed changes and Unreal assets are not reproduced.

</details>

<details>
<summary>Unity Scene View Style</summary>

ID `unity`, also `unity-editor`, `unity-scene-view`. One view, 3D first, 16-unit grid. 3D camera: first-person, 90 degree field of view.

Mouse gestures and camera keys are the same as **Unreal Editor Style**.

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Frame Selection | <kbd>F</kbd> |

**3D Wireframe** and **Draw Sector** have no key. <kbd>Alt</kbd>+right zoom, tool modes, play mode and Unity assets are not reproduced.

</details>

<details>
<summary>Godot 3D Style</summary>

ID `godot`, also `godot-3d`, `godot-editor`. One view, 3D first, 16-unit grid. 3D camera: first-person, 90 degree field of view.

| Action | 2D view | 3D view |
| --- | --- | --- |
| Pan | Middle drag, right drag | <kbd>Shift</kbd>+middle drag |
| Zoom | Wheel | — |
| Select | Left click; <kbd>Shift</kbd>+left click adds or removes one | Left click; <kbd>Shift</kbd>+left click adds or removes one; <kbd>Ctrl</kbd>+left click picks a face |
| Select by area | Left drag over empty space | — |
| Move the selection | Arrow keys nudge it | — |
| Resize the selection | Left drag its handles | — |
| Look around | — | Right drag; <kbd>Shift</kbd>+<kbd>F</kbd> turns mouse look on or off |
| Orbit | — | Middle drag |
| Move forward and back | — | Wheel |
| Fly while looking | — | <kbd>W</kbd> forward, <kbd>S</kbd> back, <kbd>A</kbd> left, <kbd>D</kbd> right, <kbd>E</kbd> up, <kbd>Q</kbd> down; <kbd>Shift</kbd> faster, <kbd>Alt</kbd> slower |

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Frame Selection | <kbd>F</kbd> |
| Top View / Front View / Side View | <kbd>Keypad 7</kbd> / <kbd>Keypad 1</kbd> / <kbd>Keypad 3</kbd> |
| Snap to Grid | <kbd>Y</kbd> |

**3D Wireframe** and **Draw Sector** have no key. Godot nodes, gizmos, keypad perspective switching and wheel flight-speed changes are not reproduced.

</details>

<details>
<summary>Blender Style</summary>

ID `blender`, also `blender-3d`. One view, 3D first, 16-unit grid. 3D camera: first-person, 90 degree field of view.

Mouse gestures and camera keys are the same as **Godot 3D Style**, except that <kbd>Shift</kbd>+<kbd>&#96;</kbd> turns mouse look on or off.

| Command | Keys |
| --- | --- |
| Open Map | <kbd>Ctrl</kbd>+<kbd>O</kbd> |
| Save Map As | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd> |
| Duplicate Selection | <kbd>Shift</kbd>+<kbd>D</kbd> |
| Select All | <kbd>A</kbd> |
| Select None | <kbd>Alt</kbd>+<kbd>A</kbd> |
| Frame Selection | <kbd>Keypad .</kbd> |
| Top View / Front View / Side View | <kbd>Keypad 7</kbd> / <kbd>Keypad 1</kbd> / <kbd>Keypad 3</kbd> |
| Snap to Grid | <kbd>Shift</kbd>+<kbd>Tab</kbd> |
| Show All Hidden | <kbd>Alt</kbd>+<kbd>H</kbd> |

**3D Wireframe** and **Draw Sector** have no key. Mesh modes, <kbd>G</kbd>/<kbd>R</kbd>/<kbd>S</kbd> modal transforms, walk gravity and .blend files are not reproduced.

</details>

## DoomEdit and BSP controls

**DoomEdit Style** (`doomedit`, also `doom-edit`, `doom3-radiant` or `d3radiant`)
uses classic Radiant plan editing with right-drag camera steering, Ctrl+right
panning and Ctrl+Shift+right mouse look. Shift+M merges brushes, Ctrl+Shift+H
isolates the selection, 0 toggles the grid, and Home or Ctrl+Tab changes the plan
projection. S opens Surface Alignment and Shift+S opens patch editing. Doom 3
formats, rendering, light/material tools, floor stepping and native texture
gestures are not added by this profile.

**BSP Quake Editor Style** (`bsp`, also `bsp-editor`, `bsp-quake-editor` or `bsp97`) uses
middle-drag mouse look, Shift+middle panning and right-click material sampling.
WASD moves, R/F rises or sinks, and Q/E turns. Ctrl+Space duplicates, Ctrl+X or
keypad minus deletes, backtick selects all, Z opens Surface Alignment and Alt+S
snaps to the grid. Shift selects in plans; a bare drag draws or resizes. The
adapted VibeStudio workspace starts with four views and a 16-unit grid. Native
selection cycling, texture drags, region tools and configuration import are not
reproduced.

Both appear in **Browse Editor Profiles…**, whose preview lists the complete
implemented defaults. Hammer++ names remain aliases for classic Hammer controls;
Hammer++ extensions and Hammer 2 do not have separate implemented presets.

## Learn more

- [Level editing](levels.md)
- [Editor profile design notes and coverage](../EDITOR_PROFILES.md)
- [Editor workflow credits and reference revisions](../CREDITS.md#editor-workflow-inspirations)
