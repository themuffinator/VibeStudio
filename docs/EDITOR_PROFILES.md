# Editor Profiles

All profiles expose the opt-in **Draw Brush** camera tool on XY/XZ/YZ planes,
with shared work-zone depth, grid, materials and plan-view square/cube modifiers.
Left-drag sets the footprint; wheel adjusts depth before release. Other gestures
retain the profile's navigation behavior. This is a common VibeStudio tool,
not a claim that every upstream editor uses the same creation gesture.
`map.draw-brush` can be assigned a key without reserving a default shortcut.
See [camera brush creation](LEVEL_EDITOR.md#camera-brush-creation) for numeric
editing, linked plan previews, source guards and remaining acceptance work.
Plan and camera brush insertion share cancellable worker preparation with all
profiles. Numeric primitives publish their validated previews through the same
source, selection, creation-layer and package guards.
The shared Objects model and background queries preserve profile-independent
selection, filtering, framing and hidden-object behavior across all 24 profiles.

Every profile shares explicit camera selection resize handles. Unmodified left
drag on an X/Y/Z face handle or its label resizes a Quake-family selection;
**Resize Selection** supplies numeric keyboard editing. Doom things expose XY
spacing handles. These targets take precedence over navigation only when hit;
Paint/Sample and mouse-look retain their modal gestures. Profile changes cancel
a pending resize. See [Camera selection resizing](LEVEL_EDITOR.md#camera-selection-resizing)
for preview, texture-policy and Doom topology limits.

Levels' opt-in **Paint**/**Sample** tools claim unmodified left input in the
camera while active. Other gestures follow the profile. Escape cancels a stroke,
then returns to Navigate; focus loss, camera/map changes and preview replacement
cancel pending edits. Paint/Sample commands are available in the palette and key
bindings without reserving existing shortcuts. Native target controls and CLI
provide the same edits without camera gestures. See
[Material Painting](LEVEL_EDITOR.md#material-painting).

Named [saved level views](LEVEL_EDITOR.md#saved-level-views) restore a camera
projection/pose and layout without changing profile gesture bindings or the
global layout preference. Choosing a layout ends the temporary override;
reapplying editor preferences restores the configured layout and camera projection.
Capture/manage/next/previous saved-view commands
are available for user bindings and reserve no default keys.

Levels' **Layout** menu can override the profile with a single plan, single
camera, camera beside plan or four synchronised views. The latter can use equal
quadrants, **Camera Above Plans** or **Camera Beside Plans**, which gives the
camera a large left pane and stacks three plan panes on the right. The override keeps the
profile's gestures and keys; **Follow Editor Profile** returns to its layout.
`vibestudio --cli editor layout [preference] --json` reads or changes this setting.
See [Four-View Workspace](LEVEL_EDITOR.md#four-view-workspace) for active-pane,
visibility and persistence behavior. `editor controls` continues to describe
the named profile's gestures with user overrides; `editor layout` reports the
effective arrangement. The profile catalog retains built-in defaults.

The Layout menu also offers **Maximize Active View**, which becomes **Restore
View Layout**, and **Equalize View Sizes**. Expansion is temporary: the prior
panes and splitter sizes return, edits keep their shared undo, and saved views
capture the underlying arrangement. Shortcuts act on the focused pane; menus
use the last focused visible pane. VibeStudio Default uses Ctrl+Space,
Hammer/Worldcraft, J.A.C.K. and Sledge use Shift+Z; both NetRadiant profiles use F12.
Classic Hammer uses Ctrl+A for equal sizing. The editor's inspectors and asset
panels remain visible. See [Four-View Workspace](LEVEL_EDITOR.md#four-view-workspace).

The same Layout menu offers linked plan centres, linked zoom, camera-follow and
one-shot centring on the camera. These choices leave profile gestures and keys
intact and default off. `editor view-links` shares the persistent defaults;
bookmarks restore their own link choices for the current map. Commands reserve
no default keys. See [Linked Navigation](LEVEL_EDITOR.md#linked-navigation).

## Gesture customization

**Levels > Controls > Customize Gestures…**, the corresponding Settings button,
and **Customize Editor Gestures** in command search open the same editor. The
2D Plan, 3D Camera and Camera Keys tabs expose 84 settings covering pan/orbit/look
buttons, modifiers, empty-space drag, selection, camera-driving behavior, wheel
behavior, flight modifiers, material sample/paint, surface paste/wrap button/modifier pairs, 22 camera
key bindings and a temporary plan-pan key. Perspective-only choices are disabled for an orbit-only
profile. A field's **Profile default** choice removes its override.

Material actions use exact button/modifier pairs in the level camera. The
left button remains reserved for selection and explicit authoring tools.
Navigation conflicts and identical sample/paint/paste chords block Apply. Older
custom navigation choices retain priority over newly inherited material
shortcuts: a conflicting inherited material action is disabled and saved as an
explicit `none` override. Older explicit sample/paint choices also take priority
over inherited paste defaults; older explicit paste bindings take priority over
new wrap and selected-value defaults. The dialog reports this adjustment. Explicit material
choices still require a conflict-free combination. Version 1 gesture files,
per-profile settings and `editor gestures` share these rules.

QeRadiant, Q3Radiant and GtkRadiant 1.4/1.6 default to middle-click material sampling and
Shift+middle single-surface painting. GtkRadiant 1.5 samples with middle and pastes
onto the hit face with Ctrl+Shift+middle; Shift+middle and Ctrl+middle stay unbound.
NetRadiant and NetRadiant Custom default
to middle-click sampling. Sampling chooses the name for the shared picker,
painting and subsequent brush creation; a brush face also supplies its mapping
and flags to the session surface clipboard. QeRadiant/Q3Radiant/GtkRadiant 1.4/1.6 use Ctrl+middle
to paste this definition onto the hit brush and Ctrl+Shift+middle onto the hit
face. Paste always uses the copied material, even after the picker changes,
and requires matching mapping formats. NetRadiant uses Shift+middle for a full
parameter paste onto the hit face; its equivalent Ctrl and Ctrl+Shift aliases
remain unbound, with the preferred chord configurable. NetRadiant Custom uses
Ctrl+middle for one seamless brush-face wrap, advancing the clipboard after
success. Shift+middle pastes native values onto the hit and selected objects;
Valve axes stay on each target, selected patches receive material only, and
copied flags apply only to the hit brush face. Alt+Shift+middle transfers values
without material/flag changes; Alt+Ctrl+middle wraps mapping only on the hit face.
Primitive UVs use actual source and target dimensions to retain texel density.
Ctrl+Shift+middle projects onto the hit and selected brush/patch surfaces;
Alt+Ctrl+Shift+middle projects mapping only. Classic/Valve parameters are copied,
primitive matrices project in world space, and patches receive control-point UVs.
Edge-on faces are permitted and reported, matching the native projection workflow.
The `camera.surfaceValues`, `camera.surfaceValuesOnly`, `camera.surfaceWrapOnly`,
`camera.surfaceProject` and `camera.surfaceProjectOnly` Button/Modifiers pairs
configure these roles. Earlier explicit value/wrap bindings take precedence over
new projection defaults. Values, Project and Wrap gestures now support held
strokes with live material previews and one undo on release. Selection is included
only on the initial hit; later hits use the current modifier role. Each wrapped
face becomes the next private source, including its package dimensions. Escape,
Cancel Stroke and stale context discard the entire stroke. Picking retains the
initial geometry/texture coverage until release so material regrouping cannot
redirect subsequent hits. These roles remain remappable in every profile;
patch-source copying/wrapping and native input acceptance remain open.
**Surfaces** offers all five paste modes in every
profile. Required map-wide Valve 220 conversion needs explicit consent there.
Work-zone depth and light
color are not sampled. Painting applies on press to the hit face, patch or Doom wall/flat, with
one undo step and the target's alignment intact. Extra modifiers, pending
preview renders and active edits/navigation cannot trigger these actions.
They keep the current tool and scene selection. Other profiles can opt in;
DarkRadiant does not inherit unaudited classic gestures. See
[Material Painting](LEVEL_EDITOR.md#material-painting) and
[Surface clipboard](LEVEL_EDITOR.md#surface-clipboard) for integration and limits.

Camera Keys provides ten fly directions, ten drive directions, a mouse-look
toggle and a temporary mouse-look hold key. Choose **Custom key** and enter a key in the native key editor,
or **None** to disable that direction. Direction bindings are unmodified single
keys; hold keys are also unmodified, and the toggle can include modifiers. Escape, Tab, modifier-only and lock keys
remain reserved. Duplicate directions, conflicting always-active fly/drive keys
and a toggle that consumes a movement key are refused before applying. Fly keys
follow the profile's mouse-look requirement and take priority over drive keys
while active. Modal profiles can give the same key different drive and fly meanings.
Modifiers required by a held-look gesture remain usable with movement keys during that gesture.
By default, turning keys strafe during free look; this is configurable. Look-up
and look-down bindings change pitch. Turn/look translation modifiers instead
strafe or move vertically. Vertical drive keys remain available.

**Hold to pan** in the Plan tab and **Hold for mouse look** in Camera Keys
provide temporary navigation without a mouse button. Release, cancellation,
focus loss, hiding/disabling the view and profile changes end the temporary
mode. Releasing hold-look restores an already active toggle. These gestures
cannot interrupt a pending geometry or paint drag. Sledge defaults both to
Space; other profiles leave them unbound. Existing profiles retain their
Space command or toggle-hand behavior.

**Possible shortcut overlaps** exposes command bindings that new navigation
keys may take priority over in a plan, in the camera or during mouse look.
The same diagnostics appear as `shortcutWarnings` in CLI JSON. These are
potential overlaps with requested profile/user shortcuts; final command
registration, current tool and focus still determine which command is available.
They do not silently remove command bindings from other studio surfaces.

Changes are staged until **Apply**. Conflicting gestures show a text diagnostic
and disable Apply. **Restore Defaults** stages removal for this profile;
closing discards changes since the last Apply. Overrides belong to canonical
profile IDs, so switching profiles preserves each scheme separately and aliases
share the same preferences. Application ends pending gestures but preserves map
bytes, selection, undo, navigation (including camera projection/FOV), current
pane layout, temporary maximization and saved-view state. All three plan panes
and the level camera adopt the same effective settings. Models retain their own
controls. The Controls reference and accessible control descriptions update.

Import/export uses a versioned VibeStudio JSON file with a base profile and only
its changed fields. Import stages a complete replacement and requires the same
profile; it cannot silently turn Hammer preferences into TrenchBroom preferences.
Malformed, oversized (over 64 KiB), unknown-version, unknown-field and conflicting
files are rejected before changing the draft or settings. Export writes atomically,
protects the active settings file and requires overwrite consent for an existing file. This is a VibeStudio format;
native upstream preference files are not imported.

`editor gestures [profile] --json` reports defaults, effective values, available
choices and current overrides. The selected profile is used when omitted.
Repeated `--set field=choice` updates distinct fields as one validated batch;
`--set field=default` removes one override. `--reset` clears the profile,
`--input` imports, and `--output` exports (with `--overwrite` for replacement).
These operations are separate. `--dry-run` validates a change without writing.

```sh
vibestudio --cli editor gestures hammer --json
vibestudio --cli editor gestures hammer --set plan.panButtons=right+middle --dry-run --json
vibestudio --cli editor gestures hammer --set plan.panButtons=right+middle
vibestudio --cli editor gestures hammer --set camera.flyKeys.forward=I --set camera.flyKeys.back=K --dry-run --json
vibestudio --cli editor gestures hammer --set camera.lookToggleKey=Ctrl+Space
vibestudio --cli editor gestures hammer --output hammer-gestures.json
vibestudio --cli editor gestures hammer --input hammer-gestures.json
```

Command shortcuts remain in Keyboard settings. The key fields use
`camera.flyKeys.forward/back/left/right/up/down/turnLeft/turnRight/pitchUp/pitchDown`,
the matching `camera.driveKeys.*` names, `camera.lookToggleKey`,
`camera.lookHoldKey` and `plan.panHoldKey`. JSON field `type` is
`choice`, `motion-key`, `toggle-key` or `hold-key`; custom key values use Qt portable key
notation, and `none` disables a key. Existing version 1 pointer-only files remain
valid. Layout, grid, linked navigation and saved
views keep their existing stores. A CLI preference change applies when the GUI
next loads that profile or reapplies preferences; it does not control a running
GUI session. Read-only/newer settings stores refuse mutation. Damaged gesture
settings produce a diagnostic and use built-in controls until corrected.

The shared `map.rotatePrecisely` command opens Rotate Selection from every
profile, with axis chosen from the current orthographic view. It adds no default
key binding. The numeric dialog provides arbitrary angles, a chosen pivot and
texture lock; the existing quarter-turn shortcuts retain their legacy behavior.

VibeStudio's level editor adapts to the editor a mapper already knows. The
goal is not to clone another editor's assets or quirks; it is to give each
mapper the mental model their hands have learned: which button pans, what a
drag over empty space does, how the camera flies, how the views are laid out,
and which keys do what.

Choose a profile in Settings > Language and Editing > Editor profile, or from
the **Controls** button on the Levels tool bar. The change takes effect at
once, without a restart. The Controls button also opens the full reference,
**Show Every Gesture and Key**, a searchable list of everything the Levels
views answer to under the chosen profile.
`vibestudio --cli editor controls <profile>` prints the same reference.

## Philosophy
- Editor familiarity is a product feature.
- One editing core serves every profile: the same documents, commands, undo
  history, and file formats. A profile changes controls and layout, never
  file compatibility.
- Profiles are data. The views read a profile's controls and nothing else, so
  a new profile is a new set of values in `src/core/level_editor_controls.cpp`
  or `src/core/level_editor_familiar_controls.cpp`,
  not new view code.
- Each profile stays documented, credited, and tested.

## How It Works

`src/core/level_editor_controls.*` describes a profile's level controls:

- **Layout** (`LevelViewLayout`): one 2D-first view with a 3D toggle, one
  3D-first view that Cycle Map View steps through 3D, Top, Front, and Side
  (TrenchBroom's one-pane layout), or the 3D camera beside a 2D view
  (Radiant's regular layout), plus a user-selectable four-view workspace.
- **2D view** (`PlanViewControls`):
  - which buttons pan, and a zoom drag;
  - whether a plain click selects (GtkRadiant's does not: there Shift+click
    selects), the keys that toggle and add on a click, the keys that drill
    down the objects stacked under the pointer, and the keys that turn any
    drag into a rubber band;
  - what a drag over empty space does: rubber band, draw a brush, or resize
    the selection toward the pointer;
  - Radiant's click that steps down the objects stacked under the pointer;
  - the middle button driving the 3D camera, and the arrow keys driving it.
- **3D camera** (`CameraViewControls`):
  - an orthographic orbit view, or a first-person perspective camera with its
    field of view;
  - the look, orbit, and pan drags;
  - Radiant's right click that toggles mouse look;
  - what the wheel does, and the Shift+wheel field-of-view zoom;
  - selection clicks, face picks among them;
  - the held keys that fly or drive the camera, with Shift faster and Alt
    slower.
- **Keys** (`LevelEditorKeyBinding`): the keys the profile gives shell
  commands. They replace the commands' own keys, and an empty list takes a
  command's keys away where the profile needs them (TrenchBroom's W flies, so
  the wireframe toggle gives W up).

`src/core/editor_profiles.*` attaches the controls to each profile. A
profile's key list becomes routed bindings, which
`StudioCommandRegistry::applyEditorProfile` installs ahead of other commands'
defaults. The shell (`ApplicationShell::applyLevelEditorProfile`) hands the
2D controls to `MapViewport::setControls` and the camera controls to
`ModelViewport::setCameraControls`, applies the layout, and on a change of
profile brings that profile's grid.

`levelEditorControlProblems()` checks that a scheme holds together: no drag
both looks and orbits, no key bound to two commands. The editor profile smoke
test runs it over every profile.

## Sidebars

The Levels page keeps its browsers and properties in two tabbed sidebars, after
Blender's: tabs of glyphs down the outer edge, each page made of collapsible
sections, and either sidebar folding down to its tabs. Which tab sits on which
side, where the gaps between groups of tabs fall, which tab is open first, and
what each tab is called are data too, in `src/core/level_sidebar.*`, keyed by a
family of profiles:

| Family | Profiles | Left | Right | Names |
|---|---|---|---|---|
| Studio | VibeStudio Default and any profile not listed | Outliner, Shapes, Entities, Textures, Models, Sounds, Prefabs | Inspector, Tools, Surfaces, Map, View, Health, History | The studio's own |
| Radiant | GtkRadiant 1.4 to 1.6, QeRadiant, Q3Radiant, NetRadiant, NetRadiant Custom, DarkRadiant, DoomEdit | Textures first, then Entities, the Entity List and the asset browsers | Entity, Surface, Filters, Map, Issues, Undo | **Entity List**, **Entity**, **Surface**, **Filters**, **Issues**, **Undo**, **Brush** |
| TrenchBroom | TrenchBroom | None | One inspector: Map, Entity, Face, then the browsers | **Entity**, **Face**, **Issues** |
| Hammer | Hammer, J.A.C.K., Sledge, BSP Quake Editor | Objects and View | Textures and Primitives first, as Hammer's texture group and object bar | **Objects**, **Properties**, **Face Edit**, **Problems**, **Primitives** |
| Doom Builder | Doom Builder 2 / X, Ultimate Doom Builder, SLADE, Eureka | None | One docker of Properties, Things and Textures | **Properties**, **Things**, **Analysis**, **Undo**, **Draw** |
| QuArK | QuArK | As the studio | Specifics, Faces and the views | **Map Tree**, **Specifics**, **Faces** |
| Blender, Unreal | Blender, Unreal Editor | Add (or Place) and the browsers | The outliner joins the properties | Blender: **Add**, **Item**, **Tool**, **Materials**; Unreal: **Place**, **Details**, **Materials** |
| Unity, Godot | Unity Scene View, Godot 3D | As the studio | As the studio | Unity: **Hierarchy**, **Materials**; Godot: **Scene**, **Materials** |

Browse Editor Profiles previews each profile's two sidebars with its names, so
the arrangement is seen before it is applied. On a Doom map, **Entities** is
called **Things** whatever the family. Moving
tabs with the tab context menu changes only the current family's arrangement,
which the settings keep as JSON; **Reset Sidebars** returns to the family's. A
saved arrangement from an older build is normalized, so tabs added since appear
where the family places them. `level_sidebar_smoke_test` checks that every
profile places every tab exactly once, the family names, and the JSON round trip;
`studio_sidebar_smoke_test` checks the widgets, folding, keys, accessibility and
right-to-left mirroring.

## Profiles

| Profile | Status |
|---|---|
| VibeStudio Default | Studio selection, navigation and a plan-first layout |
| TrenchBroom Style | Brush/camera controls adapted from TrenchBroom's defaults |
| NetRadiant Custom Style | Brush/camera controls adapted from NetRadiant Custom's defaults |
| NetRadiant Style | Standalone Xonotic defaults, 8-unit grid, 110-degree camera and Delete/Insert zoom |
| GtkRadiant 1.6.0 Style | Brush/camera controls adapted from GtkRadiant 1.6.0's defaults |
| GtkRadiant 1.4 Style | Classic Alt area selection, fixed camera steps, A/Z pitch and Shift+B texture fitting |
| GtkRadiant 1.5 Style | Shift area selection, arrow free flight and Ctrl+Shift+middle surface paste |
| QeRadiant Style | Classic Quake II camera steering and Shift+5/Ctrl+F texture fitting without Q3 patch shortcuts |
| Q3Radiant Style | Classic right-button steering, fixed movement/pitch steps and native brush/surface/patch keys |
| DoomEdit Style | Right steering, Ctrl+right pan, Ctrl+Shift+right look, Shift+M merging and Home projection |
| BSP Quake Editor Style | Middle look, Shift+middle pan, right material sampling, WASD/RF/QE and Ctrl+Space cloning |
| QuArK Style | Adapted four-view controls, plan pan/zoom and camera drive keys |
| Hammer / Worldcraft Style | Adapted four-view, Z mouse look, WASD, clipping and build/test |
| J.A.C.K. Style | Hammer-family four-view workflow with shared brush/build services |
| Sledge Style | Four views, hold-Space navigation, Q/E elevation, arrow look and Shift+arrow pan |
| DarkRadiant Style | Radiant selection, camera/plan navigation and grid controls |
| Doom Builder 2 / X Style | Plan-first, W visual view, ESDF flight, sector drawing |
| Ultimate Doom Builder Style | Doom Builder family with Q enhanced visual view |
| SLADE Style | Plan/package workflow, right pan, Q visual view, WASD flight |
| Eureka Style | Plan-first, Tab visual view, middle pan, WASD/arrows, Shift slow |
| Unreal Editor Style | Perspective-first, right-button flight, Alt+left orbit, F frame |
| Unity Scene View Style | Perspective-first, right-button flight, Alt+left orbit, F frame |
| Godot 3D Style | Middle orbit, Shift+middle pan, right-button flight, Shift+F look |
| Blender Style | Middle orbit, Shift+middle pan, keypad views and familiar selection keys |

There are 24 selectable profiles, including VibeStudio Default. They are
familiarity adaptations for VibeStudio's supported map formats, not claims of
complete upstream editor or engine emulation. The reference window includes
searchable **Profile adaptations** alongside the actual gestures. Detailed
coverage and remaining work follow below.

The CLI and stored preferences also accept `NRC`, `TB`, `GtkRadiant 1.4.0`,
`GtkRadiant 1.5.0`, `GtkRadiant 1.6.0` and `QE Radiant`. Canonical IDs remain
stable; aliases resolve to the same preset and do not create duplicate profiles.
Layout overrides, gestures, camera keys and command shortcuts remain independently
customisable for every profile.

### DoomEdit and BSP Quake Editor

`doomedit` follows [id Software's Doom 3 GPL editor](https://github.com/id-Software/DOOM-3/tree/a9c49da5afb18201d31e3f0a429a037e56ce2b9a/neo/tools/radiant),
revision `a9c49da5`, with classic Radiant plan/brush controls and a distinct
Ctrl+Shift+right camera-look gesture. Right drag steers and Ctrl+right pans;
arrow/comma/period/D/C and A/Z keys retain fixed camera steps. Shift+M merges,
Ctrl+Shift+H isolates, 0 toggles the grid and Home or Ctrl+Tab changes the plan
projection. Ctrl+U and Shift+U remain unassigned because DoomEdit uses them for
axial texture operations. Surface and patch inspectors use S and Shift+S.
`doom3-radiant`, `doom-edit` and `d3radiant` are aliases. This is an adaptation
for supported map formats: Doom 3 rendering, lights, material editing, floor
stepping, fractional grids and native texture gestures remain unsupported.
It does not claim idStudio or idTech4 format support.

`bsp` follows the [BSP 0.97q7 release settings](https://www.bspquakeeditor.com/downloads.php)
and [author's navigation release notes](https://www.bspquakeeditor.com/), reviewed
7 October 2026. Middle drag looks, Shift+middle pans and right click samples
material. WASD moves, R/F rises/sinks and Q/E turns; arrows, Insert/Page Up and
Home/End retain additional movement keys. Page Down/Delete pitches up/down.
Ctrl+Space duplicates, Ctrl+X or keypad minus deletes, backtick selects all,
Z opens Surface Alignment and Alt+S snaps. The shared four-pane workspace uses
a 16-unit grid. Shift selects in the plan and a bare drag draws or resizes.
The camera uses replacement selection with Ctrl toggling. Selection cycling
on hold, native texture drags, region bounds, alternate mouse configurations
and configuration import remain unsupported. `bsp-editor`, `bsp-quake-editor`
and `bsp97` resolve to this profile. Exact modified-pan chords take precedence
over optional mouse-look speed modifiers in every profile.

`hammer++` and `hammer-plus-plus` remain aliases for the classic Hammer preset;
the catalogue now explicitly labels that limitation. Hammer++ extensions and
Hammer 2 are not represented as separate implemented profiles.

### GtkRadiant 1.4 and 1.5

`gtkradiant-1-4` follows the [1.4.0-era ZeroRadiant source](https://github.com/TTimo/GtkRadiant/tree/5fc27697b313ddb925e57605c9983f5727a3c19f)
whose `include/version.default` identifies 1.4.0. `gtkradiant-1-5` follows the
[upstream 1.5 branch](https://github.com/TTimo/GtkRadiant/tree/017673373699174b574c92a262496826a6b409e9).
Both start with camera beside plan, an 8-unit grid, Shift selection,
Shift+right plan zoom, Space cloning, Backspace deletion, Insert/Delete zoom,
right-click free look and Ctrl+Shift+Tab framing. Outside free look,
arrow/comma/period/D/C keys step the camera by 32 units or 22.5 degrees;
A/Z pitch up/down. Modified arrows remain available to surface tools.

1.4 uses Alt area selection, middle surface sampling, Shift+middle material
painting, Ctrl+middle brush-definition paste and Ctrl+Shift+middle face paste.
1.5 uses Shift area selection and arrow-key translation during free look;
middle samples and Ctrl+Shift+middle pastes onto the hit face. 1.5 leaves the
separate Shift/Ctrl middle-button material chords unbound.

Both route Ctrl+U to brush merging, S to Surface Alignment, Shift+S to patch
editing, Shift+C to caps, Shift+T to texture lock and Shift+B to texture fitting.
Shift+arrows and Shift+Page Up/Down use the shared Surfaces target and step
settings, with centre-anchored rotation. These are independent studio
adaptations: 1.5 area selection adds rather than toggles members; native
replacement-area selection, component manipulators, fractional grids, floor
stepping, Z-checker and preference imports are not emulated. Selecting a
profile does not add another game format or engine.

### Classic QeRadiant

`qeradiant` follows the shared and Qe-specific behaviours documented in
[Eutectic's QeRadiant/Q3Radiant manual, Appendix G](https://icculus.org/gtkradiant/documentation/q3radiant_manual/appndx/sskey_dl.htm),
reviewed 7 October 2026. It uses the classic Q3Radiant camera steering and
brush controls below, with Shift+5 and Ctrl+F mapped to texture fitting.
Ctrl+F applies to the Surfaces target while a map view has focus. Q3 patch,
hide/show, select-similar and texture-lock keys are unassigned; those shared
studio commands remain available in menus and command search. Whole-entity
selection mode, animated entity previews, Alt+right texture dragging and
Z-checker remain unsupported. Surface sampling/paste keeps the common studio
clipboard rules rather than reproducing implicit selected-set changes.

### TrenchBroom Style
Reference: [TrenchBroom](https://trenchbroom.github.io/). The values follow its
defaults as of `master` `90de03c`, September 2026 (see docs/CREDITS.md).

- **Layout:** one view, 3D first, with a 16-unit grid. Space cycles the view
  through 3D, Top, Front, and Side.
- **3D camera:** first-person, with a 90 degree view.
  - Right drag looks around, and a right click opens the map menu.
  - Alt+right drag orbits the point under the pointer.
  - Middle drag pans.
  - The wheel moves along the view, and Shift+wheel zooms the field of view.
  - W A S D fly, Q flies up and X down, with Shift faster and Alt slower.
  - A left drag on the selection moves it across the ground, and Alt+left
    drag moves it up and down, on the grid.
- **Selection:** click selects, Ctrl+click adds or removes, and Shift+click in
  3D picks a face. A click on empty space deselects.
- **2D views:**
  - A left drag over empty space draws a brush: Shift keeps it square, and
    Shift+Ctrl makes it a cube.
  - Right or middle drag pans, and the wheel zooms.
  - Shift+drag draws a rubber band, which TrenchBroom lacks; VibeStudio keeps
    one within reach on an otherwise unused key.
- **Keys:**
  - Editing: Ctrl+D duplicates, Del or Backspace deletes, C is the clip tool,
    and Ctrl+Z and Ctrl+Shift+Z undo and redo.
  - Selection: Ctrl+A selects all, Ctrl+Shift+A deselects, and Ctrl+Alt+A
    inverts.
  - Hiding: Ctrl+I isolates, Ctrl+Alt+I hides, and Ctrl+Shift+I shows all.
  - Grid: + and - resize it, 1 to 9 set it (1 to 256 units), 0 shows it, and
    Alt+0 snaps.
  - View: Ctrl+U frames the selection.
  - CSG: Ctrl+K carves (CSG subtract) and Ctrl+Shift+K hollows.
- **Left alone:** TrenchBroom's Ctrl+F flips objects, but VibeStudio keeps
  Ctrl+F for Find. Alt+arrows keep going Back and Forward.

### NetRadiant Custom Style
Reference: [NetRadiant Custom](https://github.com/Garux/netradiant-custom).
The values follow its defaults at `68ecbed` (January 2026), the same revision
VibeStudio's q3map2 submodule pins (see docs/CREDITS.md).

- **Layout:** the 3D camera beside a 2D view, with a 16-unit grid.
  - Ctrl+Tab steps the 2D view through Top, Front, and Side, and keypad 7, 1,
    and 3 pick one.
  - Ctrl+Shift+C shows or hides the camera.
  - The 2D view draws where the camera stands and looks.
- **3D camera:** first-person, with a 100 degree view.
  - A right click turns mouse look on or off. While looking, W A S D and the
    arrows fly, Ctrl moves the camera sideways, and Shift moves it along the
    view.
  - Right drag strafes, and Alt+right drag orbits.
  - The wheel moves toward the pointer.
  - When not looking, the arrows drive: Up and Down move, and Left and Right
    turn.
  - A left drag on the selection moves it across the ground, and Alt+left
    drag moves it up and down, on the grid.
- **Selection:**
  - A plain click selects what is under the pointer, and clicking again in
    the same place steps to the next object down the stack.
  - Shift+click adds or removes, and Shift+drag selects by area.
  - Ctrl+click in 3D picks a face.
- **2D view:**
  - With nothing selected, a left drag draws a brush: Shift keeps it square,
    and Ctrl makes it a cube.
  - With a selection, a drag beside it resizes the sides that face the
    pointer. A drag on the selection moves it.
  - Right drag pans, a right click opens the map menu, and Alt+right drag
    zooms.
  - A middle click aims the camera at the point, and Ctrl+middle moves the
    camera there.
  - The arrows drive the camera, and Alt+arrows nudge the selection.
- **Keys:**
  - Editing: Space clones, and Del, Backspace, or Z deletes.
  - Selection: C deselects, and I inverts.
  - Hiding: H hides, and Shift+H shows all.
  - Tools: X is the clipper. Enter clips, Shift+Enter keeps both halves, and
    Ctrl+Enter flips which half stays.
  - Grid: [ and ] step it, 1 to 9 set it, and 0 shows it.
  - View: ` frames the selection.
  - CSG and entities: Shift+U carves (CSG subtract), Ctrl+G snaps to the
    grid, and Ctrl+K connects entities.

### GtkRadiant 1.6.0 Style
Reference: [GtkRadiant](https://github.com/TTimo/GtkRadiant). The values follow
its defaults on the `1.6-release` branch at `270af88` (August 2024), whose
editor is the classic QERadiant line (see docs/CREDITS.md).

- **Layout:** the 3D camera beside a 2D view, with an 8-unit grid.
  - Ctrl+Tab steps the 2D view through its planes, and Ctrl+Shift+Tab frames
    the selection in it.
  - Ctrl+Shift+C shows or hides the camera.
- **Selection:** Shift+click selects the object under the pointer, and again
  deselects it. A plain click selects nothing: a plain press only moves the
  selection, resizes it toward the pointer, or draws a brush.
  - Shift+Alt+click drills down the objects stacked under the pointer.
  - Alt+drag selects an area.
  - In the camera, Ctrl+Shift+click picks a face.
  - Escape deselects.
- **2D view:**
  - With nothing selected, a left drag draws a brush. With a selection, a
    drag beside it resizes the sides that face the pointer, and a drag on it
    moves it.
  - Right drag pans, Shift+right drag zooms, and a right click opens the map
    menu.
  - A middle click aims the camera at the point, and Ctrl+middle moves the
    camera there.
  - Delete zooms in and Insert zooms out.
- **3D camera:** first-person, with a 90 degree view.
  - A right click turns free look on or off.
  - The arrows drive it: Up and Down move, Left and Right turn. Comma and
    period strafe, D rises, and C sinks; in free look the same keys glide.
  - The wheel moves along the view.
  - A left drag on the selection moves it; with Alt, up and down.
- **Keys:**
  - Editing: Space clones and Backspace deletes; Ctrl+Z and Ctrl+Y undo and
    redo.
  - Selection: I inverts, and Shift+A selects every entity of the selected
    one's class.
  - Hiding: H hides, and Shift+H shows all.
  - Tools: X is the clipper. Enter clips, Shift+Enter keeps both halves, and
    Ctrl+Enter flips which half stays.
  - Grid: [ and ] step it, 1 to 9 set it, and 0 shows it.
  - CSG and entities: Shift+U carves (CSG subtract), Ctrl+G snaps to the
    grid, Ctrl+K connects entities, and Shift+L loads a leak trail.
- **Materials:** middle click samples the material name and Shift+middle paints
  the hit surface without changing UV alignment. Tools and selection remain in
  place. Native UV/flag copying and sampled brush depth are not implemented.
- **Not yet:** GtkRadiant's Drag Edges (E), Drag Vertices (V), Mouse Rotate
  (R) and Make Detail (Ctrl+M) remain unimplemented native modes. The camera's
  A and Z pitch keys are not bound.

### VibeStudio Default
- **Layout:** one view, 2D first, with a 64-unit grid. The 3D button shows an
  orthographic view that orbits the map.
- **2D view:** click selects, Shift+click adds, and Ctrl+click toggles. A drag
  over empty space draws a rubber band, a drag on the selection moves it, and
  the handles resize it. Middle drag pans; Space toggles a hand, so a left
  drag pans too.
- **3D view:** left drag orbits, middle drag pans (and so do Shift or Ctrl
  with a left drag), the wheel zooms, and a click selects.

### QuArK Style
QuArK now uses four synchronized panes and a 16-unit grid. Right drag pans the
plan, middle drag zooms, Ctrl+click toggles selection, and arrows with D/C drive
the camera. Ctrl+D duplicates and Escape deselects. Existing package shortcuts
stay available. Right drag looks in the camera; VibeStudio's scene tree replaces
QuArK's MDI/explorer arrangement. Auxiliary selection keys, multi-button gestures,
tree hotkeys and QuArK project files remain outside this adaptation.

### Additional familiarity families

`hammer` also accepts `worldcraft`, `valve-hammer`, `hammer++` and
`hammer-plus-plus`; `jack` accepts `j.a.c.k.` and `jackhammer`. Both use four
views, Z mouse look, middle-drag look, right-drag camera pan and middle-drag
plan pan. Brackets step the grid; Shift+X clips, Ctrl+Shift+C carves, Ctrl+E
frames, Ctrl+B snaps, Ctrl+I/L mirrors, Shift+L changes texture lock, Tab cycles
the plan projection, and F9 invokes the existing Build and Launch workflow.
Classic Hammer keeps Ctrl+H hollowing and Shift+Q deselection. J.A.C.K. instead
uses Ctrl+H to isolate, Ctrl+U or Ctrl+Shift+H to hollow, Ctrl+Q/Shift+Q to
deselect, Shift+I to invert, H/U to hide/show and Ctrl+R/Ctrl+Shift+R to turn.
Ctrl+D is not assigned to map duplication; use the shared Duplicate command.
Hammer's Ctrl+A equalizes pane sizes; J.A.C.K.'s texture-tool function keys are
not repurposed for unrelated edits. Studio file shortcuts remain available.
Space-drag, duplicate-on-drag, displacement tools, detail/structural modes,
RMF/VMF/JMF, Source 2 and Hammer++-specific tooling are not emulated.

`darkradiant` follows the Radiant camera/plan arrangement, Shift selection,
Shift+right plan zoom and toggled right-button free look. Existing Scene layers,
groups, patches and prefabs are shared. Choosing it does not add Doom 3/Quake 4
formats, fractional grid steps or Dark Mod game connection.

`doom-builder` accepts Doom Builder 2/X and `db2`/`dbx` names. W switches the
visual view; ESDF flies, C deselects, Ctrl+D draws sectors, and F9 uses the normal
build/test pipeline. `ultimate-doom-builder` (`udb`, `gzdoom-builder`, `gzdb`)
uses Q for the enhanced visual view. `slade` (`slade3`) uses Q and WASD, with
right-drag plan pan and Shift+G snap. `eureka` uses Tab, WASD/arrows, O duplicate
and Shift slower flight; digits 1–8 select grids 2–256. These profiles retain
VibeStudio's Ctrl selection and explicit paint/sample/property tools. Dedicated
V/L/S/T modes, native crosshair interactions, right-button drawing/movement,
wheel height editing, gravity and advanced source-port effects remain gaps.

`unreal`, `unity`, `godot` and `blender` offer scene-editor familiarity while
retaining idTech units and Z-up geometry. Holding the look button activates fly
keys immediately, including when Shift is already held; release ends held-button
flight. Alt+left orbits in Unreal/Unity, middle orbits in Godot/Blender, and
Shift+middle pans in Godot/Blender. Godot uses Shift+F to toggle look; Blender
uses Shift+backtick. Focus loss, hiding a view or switching profiles cancels
navigation. VibeStudio's handles/numeric tools remain the transform workflow;
upstream gizmo modes, node/mesh systems, native assets, live play and flight-speed
wheel controls are not reproduced. Unreal uses Ctrl+W duplicate; Blender uses
Shift+D, keypad period to frame, H hide and Alt+H reveal.

Aliases resolve to one canonical profile and settings persist that canonical
ID. `editor profiles`/`current` JSON include `aliases`, `adaptations` and
`referenceUrl`; `editor controls` includes `mouseLookToggleKey`. For example:

```sh
vibestudio --cli editor select hammer++ --json
vibestudio --cli editor controls udb --json
vibestudio --cli editor select godot --json
```

Profile selection does not change the map dialect or compiler target. Explicit
Layout preferences override the profile layout. Saved views, scene metadata,
undo, material/package paths, prepared builds and launch review use the existing
shared services. Unavailable tools remain explicitly documented rather than
being assigned misleading shortcuts. F9 does not bypass normal build/launch
configuration, dependency checks or deployment review.

### Standalone NetRadiant and Sledge

`netradiant` (`net-radiant`, `xonotic-netradiant`, `netradiant-classic`) is
distinct from `netradiant-custom`. Its audited defaults use an 8-unit grid,
110-degree camera, right-click mouse look, WASD while looking and arrows/A/D
for camera driving. The plan uses right pan, Alt+right zoom and middle-button
camera positioning/aiming. Delete/Insert zoom; Backspace/Z delete; Escape/C
deselect; Space clones; Shift+A selects similar; Ctrl+Y redoes; F12 maximizes.
Backtick/Ctrl+Shift+Tab frame the focused view. Camera Tab focus, discrete
movement, floor stepping, right-button selection painting, selection-box
semantics and clone-with-unique-targets remain adaptations/gaps.

`sledge` (`sledge-editor`, `sledge2`, `sledge-2`) follows the released 2.0.7.2
source: four views, 16-unit grid and a 60-degree camera. Hold Space to pan the
plan or temporarily look in the camera; Z toggles mouse look. WASD moves while
the camera has focus, Q rises and E descends. Arrows turn/look in both modes;
Shift+arrows translate in the camera and pan by a quarter of the plan view.
Ctrl/Alt+arrows remain outside camera navigation. During mouse look, right
mouse pans and left+right moves along the view; Shift/Ctrl change flight speed
without changing pointer motion. Ctrl+A selects all, Ctrl+H isolates,
Ctrl+Shift+H hollows, Shift+X clips and Shift+Z maximizes. F9 enters the shared
build/test pipeline. Speed curves/multipliers, numeric zoom presets, linked
wheel zoom, tool modes, duplicate-on-drag, selection-box/grouping semantics,
RMF/VMF and native preference import remain gaps. Core editing still uses the
shared document, undo, assets and compiler services.

The pinned upstream files and GPLv3 compatibility reviews are recorded in
[Credits](CREDITS.md#editor-workflow-inspirations). No upstream implementation
or assets were imported. These profiles have separate canonical preference
stores, and their hold/pitch controls use the same validated GUI/CLI gesture
format as the other profiles.

### Classic Q3Radiant

The `q3radiant` profile follows id Software's three-button defaults, audited at
revision `dbe4ddb10315479fc00086f08e25d968b4b43c49`. It uses a camera beside the
plan and an 8-unit grid. Right drag pans the plan; middle aims the camera and
Ctrl+middle places it. Shift toggles selection, Shift+Alt drills through stacked
objects, and unselected empty-space drags create brushes with the current
material and work zone. Insert/Delete zoom and Ctrl+Tab changes the plan axis.

In the camera, hold right to steer: distance above/below the centre controls
forward/backward movement, and distance left/right controls turning. Horizontal
steering has a dead zone and diminishes near the top/bottom edges. Movement
stays on the ground plane at up to 400 units/second, with up to 270 degrees/second
turning. Centre stops movement; release, Escape, focus loss, hiding/disabling,
profile changes, modifier changes, saved-view restoration and preview replacement end steering.
Ctrl+right pans using the studio's existing camera pan behavior.

Up/Down move 32 units per press/repeat, Left/Right turn 22.5 degrees, comma/period
strafe, D/C rise/sink and A/Z change pitch, bounded to 85 degrees. Plan arrows
use the same fixed forward/turn steps regardless of grid. Alt+arrows still nudge;
reserved Shift/Ctrl arrow texture chords do not move geometry. Space duplicates,
Backspace deletes, Shift+U carves, Ctrl+U merges, S opens Surface Alignment,
Shift+S edits patches, Shift+C opens patch caps and Shift+T toggles texture lock.
Shift+arrows adjust U/V texture offsets, Shift+Page Up/Down rotate by the
negative/positive surface step and Shift+5 fits one repeat. These seven keys
come from the same pinned `MainFrm.cpp` command table. They use the visible
target and step controls in **Levels → Surfaces**. The default studio steps
are 8 texels and 15 degrees per session; native Q3Radiant preferences are not
imported (its audited rotation default is 45 degrees). Shifts use U/V axes,
rotation is anchored at the winding centroid and Fit retains the current UV
orientation. Native camera-relative face shifts and origin-based mapping
behavior are not reproduced. Ctrl+arrow additive scaling remains unbound;
the studio's explicit grow/shrink controls use multiplicative size changes.
These commands share normal material/package resolution, scene locks, undo,
saving, validation and compiler workflows. Merge, Surface Alignment, Edit Patch
and Cap Patch also resolve through command search and keyboard customization.

`q3-radiant`, `quake3-radiant` and `quake-iii-radiant` are aliases of this independent
preference store. Settings, first-run setup, Controls help and CLI use the same
descriptor. Gesture customization exposes `camera.driveButton`,
`camera.driveModifiers`, `camera.discreteDriveKeys` and `plan.fixedCameraSteps`;
ambiguous steering/look/orbit/pan gestures are refused. Existing version-1
gesture files remain valid. New fields can also be used with other perspective
profiles; default-off fields preserve their existing behavior. Fixed drive steps
leave active fly bindings continuous, including a held mouse-look gesture.

Native preferences, Z-checker, floor stepping, End camera levelling, terrain,
bend/rotation modes, Ctrl+right plan clip-point placement and UV-copying texture
gestures remain unsupported. Middle samples the material name; Shift+middle
paints one hit surface while preserving its UVs. Ctrl+middle brush copying and
Ctrl+Shift+middle UV copying remain unbound. The inherited Alt area selection and grid keys 8/9
are studio extensions to the audited native defaults.
The camera uses the studio's vertical field-of-view sizing and pan speed;
native Q3Radiant uses horizontal sizing and pixel-based panning. Shared brush
resize, selection and patch dialogs remain studio workflows. Native input
acceptance is still separate from semantic Qt tests. This profile does not
stand in for QERadiant, DoomEdit or game-specific Radiant variants.

### Coverage still needed for professional acceptance

The catalog covers major brush, Doom and modern scene-editor families. Additional
game-specific Radiant variants,
Qoole/BSP, DEdit and older UnrealEd require separate reference audits before any
claim of matching their defaults. These are not silently treated as equivalent
to newer profiles. Native preference-file import and native mode/tool parity,
complete component gizmos, real keyboard/screen-reader
acceptance and native macOS/Linux checks remain open. See the broader
[professional editor acceptance matrix](LEVEL_EDITOR.md).

The catalog caches translated descriptors on the application thread and
invalidates them on language changes. Worker lookups use independent data.
Control validation rejects ambiguous orbit/pan gestures, invalid grid/FOV
defaults, malformed shortcuts and duplicated motion keys. The core suite checks
gesture routing without injecting input. The dedicated profile UI suite checks
live shortcuts, settings, layout overrides, document preservation, CLI aliases,
searchable adaptations and 100%/200% high-contrast RTL widget layouts.
The reference wraps long descriptions and gesture lists as the window resizes.
Offscreen tests exercise Qt services and widget rendering; they do not establish
native mouse, keyboard or screen-reader acceptance on any platform.

Validation on 2026-10-05 used the Windows Clang debug build with warnings treated
as errors. The 18-profile extension passed eight focused suites: profile core/UI,
gesture core/UI, saved views, command semantics, the shared viewport and material
painting. Tracked source and binary hashes stayed unchanged during that run.
The tests cover live profile changes, canonical CLI aliases, all 60 preference
fields, temporary-navigation lifecycle, undo/navigation preservation and refusing
to start mouse look during a pending material stroke. The Controls reference and
gesture pages were rendered and inspected at 100% and at 200% with high contrast,
RTL layout and expanded text, including scrolled hold/pitch controls and footer
buttons. Catalog lookup and complete test-loop timings do not establish production
map or frame-time performance.

## Behaviour Shared By Every Profile
- Every command stays in the menus and the command palette, whatever key a
  profile gives it; a user's own keys (Settings > Keyboard) outrank a
  profile's.
- A command with no VibeStudio key, such as Cycle Map View or Zoom In, takes
  a key only from a profile or the user, and that key acts on the Levels page
  alone.
- Cycle Map View, Next 2D View and 3D View shortcuts require focus in a level
  viewport. Tab and Space continue to serve normal focus and button behavior
  in inspectors and other page controls. Viewport-local selection cycling
  yields when a profile or user command reserves its key.
- The 3D view claims its fly keys only while it has focus; elsewhere those
  keys keep their commands. Flight accepts the profile's speed/look modifiers;
  other combinations remain available to commands. Gesture preferences report
  overlaps when a custom navigation key would take priority over a command.
- Brushes drawn in 2D take the texture picked in the Levels Textures tab, else
  the last one drawn with, else the map's most used texture. Along the axis
  the view hides, they span the last selection's extent (Radiant's work zone),
  or 0 to 64 units.
- Frame Selection includes visible entity-owned geometry even without an explicit
  origin, plus the combined extent of point entities, Doom vertices and straight
  lines in every plan projection. Selection geometry is reused
  across repeated readouts and framing, and refreshed by edits, undo, visibility
  changes and reloads. Resize still uses complete entity-owned geometry.
- All plan profiles share the bounded projected-wire renderer. Exact coincident
  edges draw once within each pen-style run; the complete objects remain in
  selection cycling, scene tools, undo and compiler output. Pan, zoom, display
  scale and contrast refresh the drawing image while editing overlays stay live.
- Larger Quake plan scenes prepare their images in the background for every
  profile. New navigation replaces pending work and repositions the prior image
  until completion, with **Updating view…** in the HUD and accessible description.
  Selection/source changes retire old highlights, and picking uses current full
  geometry. Small scenes and oversized image targets retain complete immediate
  drawing; this introduces no profile-specific control or setting.
- All plan profiles highlight selected Quake brush edges and curved patch borders
  with thicker dashed outlines. Selecting an entity includes its visible owned
  geometry; selecting a child too does not double the stroke. Centre markers,
  primary feedback, hidden-object handling and full-source editing bounds retain
  their shared behavior. This feedback adds no profile-specific gesture.
- All plan profiles reuse unchanged grids and exact-phase member-ring stamps.
  Cache limits retain complete drawing and do not alter picking or selection.
  Primary labels stay near their marker, inside the pane and clear of the HUD,
  with direction-aware elision and a complete accessible selection description.
- Mouse look and flying follow the user's own hand, so reduced motion leaves
  them be; the navigation rail's slide is what it turns off.
- A selection dragged in the 3D camera moves as one undo step, just as a drag
  in 2D does, and Escape drops a move still in progress.
- Hidden objects leave the 3D view as well as the 2D one. The clip tool cuts
  in 2D, so choosing it while the 3D view is showing brings the 2D view
  forward.
- Each Levels view tells a screen reader its controls under the chosen
  profile after its own description, so they stay heard as the view changes.

## Reporting
- `vibestudio --cli editor controls <profile> [--json]` prints a profile's
  layout, grid, effective gestures and profile keys. `vibestudio --cli editor profiles
  --json` carries the built-in `controls` object per profile, with
  `unresolvedPresets` and `placeholder`.
- `editorProfileSummaryText()` lists the controls and whether all presets
  resolve.
- Every binding reports `implemented`, computed from
  `shellCommandIdExists()` and never asserted; a binding that takes a
  command's keys away carries `clearsKeys`.
