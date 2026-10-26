# Level editing

The **Levels** page opens, edits, checks and saves maps for Doom-family games (Doom and Hexen maps in WADs, including
UDMF) and Quake-family games (Quake, Quake II and Quake III `.map` files), then hands them to the **Build** page.

> [!NOTE]
> **Status: Partial.** Opening, editing, undo, validation and saving work for every format below and are covered by
> automated tests, but none of it has been proven on real production maps. UDMF maps get property and transform editing
> only, and some tools from established editors are still missing; the
> [coverage list](../LEVEL_EDITOR.md#coverage-of-other-editors) says which.

The [TB/NRC command audit](../plans/level-editor-commands.md) links discovered
commands to their upstream source and records reviewed equivalents and gaps.
Unreviewed entries have no parity status yet; command counts do not establish
production readiness.

## Open a map

1. Choose **Levels** on the left rail, or press <kbd>Ctrl</kbd>+<kbd>2</kbd>. On macOS, <kbd>Ctrl</kbd> is
   <kbd>Cmd</kbd> throughout this page.
2. Choose **Open Map** in the page header, or **File** > **Open Map…** (<kbd>Ctrl</kbd>+<kbd>M</kbd>).
3. Pick a `.map` or `.wad` file. A progress window shows each loading step; **Cancel** keeps the map you had open.
4. A WAD with several maps opens its first map. To open another, choose it in the map list beside the path on the
   document bar, or type a name such as `MAP07` or `E2M1` there.

VibeStudio works out the game from the file. If it guesses wrong, change the engine list on the document bar from
**Auto** to **idTech1**, **idTech2** or **idTech3** and choose **Reload**. The empty page lists **Recent maps**, and
**File** > **Go to File…** (<kbd>Ctrl</kbd>+<kbd>P</kbd>) finds a map in your project or the open package. A map opened
from a package is a temporary copy, so its first save asks where to keep it.

To start a new map, choose **New Map** (<kbd>Ctrl</kbd>+<kbd>N</kbd> while the page has focus). Pick a **Game format**
(**Quake**, **Quake II**, **Quake III Arena**, **Doom (binary)** or **Hexen (binary)**), then **Room with player start**
or **Empty map**. Texture names refer to your own assets.

| Format | What you can do |
| --- | --- |
| Doom and Hexen maps in a WAD | Open, edit things and geometry, save |
| UDMF (`TEXTMAP`) maps in a WAD | Open, edit properties, move, rotate, mirror, snap, resize, duplicate things and save |
| Quake and Quake II `.map`, classic or Valve 220 faces | Open, edit, save |
| Quake III `.map` with `brushDef`, `brushDef3`, `patchDef2` or `patchDef3` | Open, edit, save; create and reshape patches |

## Find your way around

| Area | What it holds |
| --- | --- |
| Header | **Dependencies**, **Package Map**, **Save**, **New Map**, **Generate**, **Edit with AI**, **Open Map** |
| Document bar | The map path, the WAD's map list, the engine list, **Reload**, the compiler profile, **Run Profile**, **Copy CLI** |
| Left sidebar | **Outliner**, **Shapes**, **Entities**, **Textures**, **Models**, **Sounds**, **Prefabs** |
| Centre | The view toolbar; the tool bar (**Select**, **Draw Brush**, **Clip**, **Paint**, **Sample**, **Draw Sector**) with the grouped authoring menus; the material strip; the 2D and 3D views; and a readout of what is under the pointer |
| Right sidebar | **Inspector**, **Tools**, **Surfaces**, **Map**, **View**, **Health**, **History** |

### Use the sidebars

Each sidebar is a column of tabs beside the views, as in Blender. Choose a tab to show its page, and choose the
current tab again to fold the sidebar down to its tabs and give the views the room. A page is made of sections: choose
a section's heading, or press <kbd>Left</kbd> and <kbd>Right</kbd> on it, to close or open it, and each remembers how
you left it. A page's heading counts what it holds, such as the outliner's objects or the health problems, and so does
its tab's tooltip.

- The **Tools** tab holds every editing command in one place, as Blender's Tool tab does, grouped into **Brush**,
  **Vertices**, **Transform**, **Align**, **Select**, **Navigate**, **Entities**, **Groups**, **Surfaces**,
  **Patches**, **Patch Rows and Columns**, **Sectors and Lines** and **Region and Build**. Each button is the command itself, so it greys out, stays pressed and takes keys just as in the menus; a
  group the map's format cannot use is hidden.
- Right-click a tab to move it to the other sidebar, fold the sidebar, show captions under the tab icons, or
  **Reset Sidebars** to where your editor profile keeps them.
- The **View** menu has **Level Sidebars** commands to show each tab and to fold either sidebar. Like every
  command, they are in command search and can be given keys in **Help** > **Keyboard Shortcuts**.
- **Show Surfaces** also hides the **Surfaces** tab again when it is showing: its sidebar goes back to the tab
  it showed before, or folds if it was folded. The NetRadiant Custom and NetRadiant profiles give it
  <kbd>S</kbd>, as NetRadiant Custom shows and hides its Surface Inspector; in mouse look <kbd>S</kbd>
  still moves the camera.
- Your [editor profile](editor-profiles.md) decides where the tabs start and what they are called. Radiant profiles
  open on the texture browser and call the outliner **Entity List**. TrenchBroom keeps everything in one inspector
  on the right. Hammer-family profiles have **Objects**, **Properties**, **Face Edit** and **Primitives**, and Doom
  Builder profiles **Things**. On a Doom map, **Entities** is called **Things** whatever the profile.
- Each family of profiles remembers its own arrangement, so moving a tab under a Radiant profile leaves the
  TrenchBroom one as it was.

### Choose a tool

The tool bar above the views says what the mouse does. **Select** picks, moves and resizes objects. **Draw Brush**
draws the shape chosen on the **Shapes** tab in the 3D view; its icon shows which. **Clip** cuts brushes along a line,
**Paint** and **Sample** put on and pick up materials in the 3D view, and **Draw Sector** traces Doom sectors. Only
the tools the open map can use are shown, and the material strip below holds the settings of the camera tools.

## Look around the map

- **2D views.** Choose **Top (X/Y)**, **Front (X/Z)** or **Side (Y/Z)** in the projection list. The wheel zooms;
  how you pan, select and draw depends on your [editor profile](editor-profiles.md). **Zoom to Fit**
  (<kbd>Home</kbd>) frames the whole map and **Zoom to Selection** (<kbd>F</kbd>) frames the selection.
- **Go somewhere.** **Centre Views on Selection** moves every 2D view to the selection without changing its zoom, as
  Radiant's Center does. **Go to Coordinates…** takes the 2D views, and the camera if you tick it, to a point you
  type, as Hammer does. **Find Brush by Number…** selects and frames the object a compiler names, such as
  `Entity 0, Brush 12`: paste the compiler's line and the numbers are read from it. Brushes and patches are counted
  together from 0, in the order the file lists them, as compilers count them. All three are on the **Tools** tab's
  **Navigate** group.
- **Grid.** Choose **Grid 0.125** to **Grid 1024** in the grid list above the views, in the **View** tab's **Grid**
  list or in command search, or press <kbd>[</kbd> and <kbd>]</kbd> to halve or double it. **Smaller Grid** stops at
  0.125 units and **Larger Grid** at 1024. With **Snap** ticked, drags, arrow-key nudges, drawn brushes, clip points
  and camera drags move in whole grid steps, so on **Grid 0.25** they move a quarter of a unit at a time. When you
  zoom out, a fine grid's lines are drawn further apart so they do not fill the view; the view's status lines say how
  far apart they are. Doom and Hexen maps store whole units, so while one is open the grids below 1 unit are greyed
  out, a finer grid changes to **Grid 1** and the status bar says so; UDMF and Quake-family maps keep the fractions.
  In the NetRadiant Custom and
  NetRadiant profiles, choosing or stepping the grid also turns **Snap** on, as those editors do, and the status bar
  says so; turn this off with **Choosing or stepping the grid turns Snap on** on the **Editing** tab of
  **Customize Gestures…**. Your grid size is kept for the next session.
- **3D view.** Choose **3D** to show the camera. The VibeStudio Default profile orbits an orthographic view of the map;
  most other profiles fly a first-person camera. Tick **Textures** above the views to draw package images: open the
  game's package or asset folder on the **Packages** page first. **View** > **3D Wireframe** (<kbd>W</kbd>) shows edges
  only.
- **Choose how the camera draws.** **View** > **Camera Display** and the **View** tab's **Display** section set what the
  camera draws, and your choice is kept for the next session. **Textures in Camera** (the same as the **Textures** box
  above the views) switches the faces between package images and flat-shaded colours. **Edges in Camera** outlines
  every brush face and patch quad, not the triangles they are drawn with. **3D Wireframe** hides the faces and leaves
  the edges; turn it off and the faces come back as they were, textured or flat. The camera keeps these choices when
  it redraws after an edit. **Show Textures in Camera**, **Hide Textures in Camera** and **Hide Faces in Camera** set
  the faces outright, as TrenchBroom's view options do. **Next Camera Mode** and **Previous Camera Mode** step through
  **Wireframe**, **Flat Shaded**, **Textured** and **Textured with Edges**, and the status bar says which one is
  showing (<kbd>Shift</kbd>+<kbd>]</kbd> and <kbd>Shift</kbd>+<kbd>[</kbd> in the NetRadiant Custom profile). These
  commands work while the camera is showing; **Textures in Camera** and **Edges in Camera** can be changed at any time.
  They change only the picture, never the map. Give any of them a key in **Help** > **Keyboard Shortcuts**.
- **Aim the camera.** **View** > **Camera** moves only the 3D camera: the 2D views, the map and its undo history stay
  as they are. **Level Camera** keeps the camera where it stands, takes away its pitch and turns its heading to the
  nearest 22.5 degrees, as Radiant's **Center** does (<kbd>End</kbd> in the NetRadiant Custom, NetRadiant, GtkRadiant,
  Q3Radiant and DoomEdit profiles). **Focus Camera on Selection** keeps the camera's heading and pitch and moves it
  along its view until the whole selection fits, never closer than 64 units to its centre. Unlike **Frame
  Selection** it leaves the 2D views where they are. With nothing selected it focuses on the last selection you made
  in this map, and hidden objects never count. **Reset Camera Zoom** returns the field of view to your profile's
  after a <kbd>Shift</kbd>+wheel zoom or a saved view's own (<kbd>Ctrl+Alt+Z</kbd> in the TrenchBroom profile). In the
  VibeStudio Default profile's orbiting view, **Level Camera** levels the orbit and **Focus Camera on Selection** turns
  it about the selection. All three are in the **Tools** tab's **Navigate** group and in command search.
- **Keep the selection in view.** In the NetRadiant Custom and NetRadiant profiles, hold <kbd>Tab</kbd> during mouse
  look: the camera stays on the line through the selection's centre as you turn and fly, and the wheel moves it nearer
  or farther, eight notches halving or doubling the distance. Release the key, or leave mouse look, to fly freely
  again. Outside mouse look <kbd>Tab</kbd> still moves keyboard focus. Choose another key, or none, with **Hold during
  mouse look to keep the selection in view** on the **Camera Keys** tab of **Customize Gestures…**; any profile with
  a first-person camera can use it. `map camera-focus` reports the same camera position from the command line.
- **Layout.** The **Layout** menu shows one 2D view, one 3D view, the camera beside a 2D view, four views,
  **Camera Above Plans** (a wide camera above top, front and side), or **Camera Beside Plans**
  (a tall camera beside three stacked plans). Drag the dividers to size the panes;
  each layout remembers its own proportions. Grid, snap and selection framing remain available in a maximised camera.
  **Follow Editor Profile** returns to your profile's arrangement. **Maximize Active View** enlarges the focused pane
  until you restore it, **Equalize View Sizes** evens out the panes, and the link options keep the 2D views centred
  together or following the camera. **Saved Views** stores named camera and plan positions for this map. The
  **Layout** section of the **View** tab offers the same choices as tiles, each picturing its panes with the camera
  pane filled; point at a tile for its full name.
- **Choose the plan panes.** In the three-plan layouts (four views, **Camera Above Plans** and **Camera Beside Plans**)
  turn **Show Top View**, **Show Front View** and **Show Side View** off to hide a plan pane, for example to keep only
  the front and side views beside the camera. They sit in the **Layout** menu, the **View** menu, command search and as
  check boxes in the **Layout** section of the **View** tab, and have no keys until you give them one. One plan pane
  always stays: the last shown pane's command is unavailable. Hiding the pane you were working in makes the next shown
  pane active; a row left empty closes up. **Top View**, **Front View**, **Side View** and the projection list bring a
  hidden pane back, and **Next 2D View** steps only through the shown panes. Each combination of panes remembers its
  own proportions. The choice is saved, applies to every three-plan layout and waits unused in the one-plan layouts;
  it never changes the map or its undo history. `editor layout --panes front,side` sets it from the command line.
- **Show.** The **Show** menu chooses what the 2D views draw: **Things** (Doom) or **Entities** (Quake), **Sector Fill**
  (Doom), **Grid**, **Grid Coordinates**, **Block Grid** (Quake), **Vertices**, **Labels**, **Target Links** and **Linked
  Groups** (Quake). The **View** tab has the same options with the grid, snapping, texture locks, layouts and view links.
- **Read coordinates off the grid.** Turn on **Show** > **Grid Coordinates** to number the grid lines in every 2D view,
  as NetRadiant Custom's **Show Coordinates** does: values across along the bottom edge, values up along the left edge
  (the right edge in right-to-left languages). The numbers are spaced so they never crowd each other, show the
  fractions a fine grid needs, and stay clear of the status tags in the corners. The pointer readout below the views
  still gives the exact point under the pointer.
- **See the compiler's blocks.** Turn on **Show** > **Block Grid** on a Quake-family map to draw dashed lines where the
  compiler first splits the world into blocks, as NetRadiant Custom's **Show Blocks** does. The size comes from the
  worldspawn `_blocksize` key, which you can set in the **Map** tab: one number for every axis, three for x, y and z,
  and 0 to leave an axis unsplit. Without the key the blocks are 1024 units. The older `blocksize` and `chopsize` keys
  are read too, and a value is read exactly as q3map2 reads it, so a lone second number is ignored and the status bar
  says so. The status tag shows the size, and inside a region the lines stay within it. q3map2-family compilers such
  as VibeMap3 split on these lines; Quake and Quake II compilers such as VibeMap2 do not, so treat the lines as a guide
  for Quake III-family builds. **Export Image** and `map render --block-grid` draw the same lines.
- **Toggle them from anywhere.** **View** > **Show Grid Coordinates** and **Show Block Grid** do the same as the
  **Show** menu, with check marks, and are in command search. Neither has a key, as in NetRadiant Custom; give them
  one in **Help** > **Keyboard Shortcuts**. Both are off until you turn them on, the choice is kept for the next
  session, and neither ever changes the map or its undo history.
- **Filters.** The **Filters** section of the **View** tab hides whole kinds of object in every view, each with a
  count: world brushes, entities, brushes, brush entities, func groups, patches, detail, structural, clip, player
  clip, monster clip, weapon clip, bot clip, hint, skip, caulk, nodraw, area portals, cluster portals, light grid,
  sky, liquids, fog, translucent, decals, point entities, lights, triggers, monsters, items, player starts, paths and
  models, or on a Doom map each category of thing. A ticked kind is shown. The same kinds are checkable commands under **View** > **Filters**,
  in the **Filters** submenu of the view bar's **Select** menu and in command search, so you can give them keys;
  the NetRadiant Custom and NetRadiant profiles use theirs: <kbd>Alt+1</kbd> **World Brushes**, <kbd>Alt+2</kbd>
  **Entities**, <kbd>Alt+3</kbd> **Area Portals**, <kbd>Alt+4</kbd> **Translucent**, <kbd>Alt+5</kbd> **Liquids**,
  <kbd>Alt+6</kbd> **Caulk**, <kbd>Alt+7</kbd> **Clip**, <kbd>Alt+8</kbd> **Paths**, <kbd>Alt+9</kbd> **Cluster Portals**, <kbd>Alt+0</kbd>
  **Lights**, <kbd>Ctrl+D</kbd> **Detail**, <kbd>Ctrl+Shift+D</kbd> **Structural**, <kbd>Ctrl+H</kbd> **Hint**,
  <kbd>Shift+M</kbd> **Models**, <kbd>Ctrl+Shift+T</kbd> **Triggers**, <kbd>Alt+M</kbd> **Bot Clip** and
  <kbd>Shift+D</kbd> **Decals** (<kbd>Ctrl+P</kbd> stays **Go to File**, so **Patches** has no key, and **Light Grid**
  has none in NetRadiant Custom either; **Skip** and **Fog** have no key there). **Invert Filters** hides every kind that is
  shown and shows every kind that is hidden, as Radiant's **Invert filters** does, and **Show Every Kind** brings
  them all back.

  The texture kinds read each face's texture and content flags. **Area Portals** takes a brush with any areaportal
  face or the area portal flag, and Quake II's func_areaportal entities. **Cluster Portals**, **Light Grid** and
  **Bot Clip** (botclip or donotenter) take a brush only when every face is of that kind, and **Decals** a brush with
  any face whose texture is under `decals/`; those four are for Quake III maps, where bot clip also leaves **Clip**.
  **Structural** is the opposite of **Detail**: on Quake II and III maps every brush without the detail flag outside
  func_detail, and on Quake maps worldspawn and every brush entity but func_detail. **Caulk** hides brushes caulked on
  every face, and while it is off the camera also leaves out caulk faces on other brushes, so you see the surfaces
  players see; the 2D views keep each brush's whole outline, as Radiant's do, and the status bar says how many faces
  went. **Hint** takes a brush with a hint face, and **Skip** a brush skipped on every face; while **Skip** is off
  the camera also leaves out the skip sides of other brushes, so hiding it leaves a hint brush's hint face. When the
  faces that go one by one cover a whole brush, say caulk on some sides and skip on the rest, the brush leaves the 2D
  views too. On a Quake map, skip counts as caulk and only ericw-tools' hintskip counts as **Skip**; Quake II maps
  also read the hint and skip surface flags. **Fog** takes Quake III fog volumes: a shader with surfaceparm fog or fogParms, the fog
  content flag, or else, where no shader is known, a texture in a `fog` or `fogs` folder or named after fog, such as
  `sfx/fog_red` (not `fogged_glass`).

  The material kinds read what a surface is made of. On a Quake III map the **Filters** section reads the shader
  scripts of the package you have open, staged edits included, and says how many shaders it read; press **Reload
  Shaders** after changing scripts outside the studio (the camera's **Reload** reads them again too). A face whose
  shader the package defines, or whose image it holds without a script, then goes by that shader: **Translucent**
  takes a brush or patch with any qer_trans, alphaFunc, qer_alphafunc or fog shader, as Radiant's **Translucent**
  filter does; **Liquids** takes surfaceparm water, slime or lava, so a lavarock wall stays and a liquid under any
  name goes; **Sky** takes skyParms or surfaceparm sky; and surfaceparm playerclip, monsterclip, botclip and nodraw
  feed **Player Clip**, **Monster Clip**, **Bot Clip** and **Nodraw**. Faces the package knows nothing about keep
  the name rules, and without a package **Translucent** finds nothing on a Quake III map. Quake II maps read each
  face's own flags instead (the 33% and 66% translucency flags, nodraw and the clip contents), and Quake maps the
  texture names, so `clip` counts as player and monster clip and a quoted `{` fence texture as translucent.

  **Clip** still hides every kind of clip; **Player Clip**, **Monster Clip** and **Weapon Clip** (weapclip and
  fullclip, which stop shots too) hide one at a time, and all four take clip-shaded patches as well as brushes made
  wholly of clip. **Nodraw** hides brushes that draw nothing in game, every face nodraw, so on Quake III maps it also
  takes caulked, clip and trigger brushes whose shaders are nodraw.

  **World Brushes** counts worldspawn, func_group and func_detail as the world, as Radiant does, and **Entities**
  is everything else, doors and triggers included. **Brushes** hides every brush whoever owns it but leaves patches
  to **Patches**, as TrenchBroom's **Show Brushes** does. **Point Entities** leaves lights and models to their own
  filters, so you can hide the clutter and keep the lights; a door or other brush entity with no brushes yet stays
  under **Brush Entities**. **Func Groups** hides func_group entities only; the **Scene** tab's groups have their own
  visibility. Filters are separate from hidden objects, so **Show All Hidden**
  leaves them on, and they only change what you see and can pick, never the map. `map filters` reports the same
  kinds and counts from the command line.
- **Filter tags from your game or project.** A game or project can name its own kinds as tags, the way TrenchBroom's
  game configurations do: by texture pattern, Quake III surfaceparm, content or surface flag, or brush entity class.
  Put them in the project manifest under `levelFilterTags`, either written out or as the path of a tag file beside
  the project, or choose **Tag File…** under **Tags** in the **Filters** section. A TrenchBroom game configuration
  (`GameConfig.cfg`) works as it is: its comments, bare names and trailing commas are read as TrenchBroom reads
  them, and a pattern such as `"\**"` keeps its backslash, so it takes textures whose names start with `*`. If a file
  cannot be read, the message names the line. Each tag gets a check box with its count and a checkable **Toggle**
  *tag* **Visible** command in command search, the **Filters** menus and **Help** > **Keyboard Shortcuts**, so you
  can give it a key. A texture, surfaceparm or flag tag hides a brush whose every face matches and, in the camera,
  the matching faces of other brushes one by one; a brush whose faces all go under several tags or kinds together is
  hidden whole. A class tag hides the brush entities of that class. Surfaceparm tags read the shaders of the package
  you have open, as the material kinds do. Tags are remembered with the other kinds, **Invert Filters** flips them
  too and says how many it hid, and **Forget Tag File** stops using the file. The **Tags** line says where the tags
  came from and how many entries it skipped; point at it for the details. Names written entirely in non-Latin
  scripts keep their display text and receive a stable generated command ID.

  ```json
  "levelFilterTags": {
    "brush": [ { "name": "Triggers", "match": "classname", "pattern": "trigger_*" } ],
    "brushface": [ { "name": "Fog", "match": "surfaceparm", "pattern": "fog" },
                   { "name": "Water", "match": "texture", "pattern": "*water*" },
                   { "name": "Trans", "match": "surfaceflag", "flags": [ "trans33", "trans66" ] } ]
  }
  ```

  Texture patterns match the texture's own name, or its path when the pattern has a `/`; `*` and `?` are wildcards,
  `\` makes the next character plain, and case does not matter. Flags are the game's names (as the **Surfaces** tab
  shows them), numbers, or the names a game configuration's `faceattribs` lists.
- **Entity classes.** The **Entity Classes** section of the **View** tab lists every class of the loaded entity
  definitions and every class the map uses, grouped by the part of the name before the first `_` and counted.
  Untick a class, or a whole group, to hide every entity of it in all the views, including ones you add later;
  **Hide All Classes** and **Show All Classes** do the lot, and **Find a class** narrows the list. Each class also has
  a checkable **Toggle** *class* **Visible** command, as in TrenchBroom, that you can find in command search and give
  a key. Class filters last until you open another map and never change the map; **Show Every Kind** clears them
  too. A class whose name holds a comma has no filter, as commas separate the lists filters are kept in.
  `map visibility --class` and `map filters --class` report what they hide.
- **Region.** To work on one area of a big map, choose **Set to Selection** or **Set to View** in the **Region**
  section of the **View** tab (or **Select** > **Set Region to Selection**). **Draw Region** lets you drag the box in
  a 2D view, through the whole map's depth; **Set Region to Brush** uses the selected brushes' bounds and removes
  them, as Radiant does (**Undo** brings them back and keeps the region); and **Region Bounds…** takes the corners
  you type. The views shade or hide what lies outside the box, as Radiant's regions and Hammer's cordon do, and
  **Select Everything in Region** selects what it keeps. VibeStudio remembers each map's region and brings it back
  when you open the map again; **Clear Region** brings the whole map back and forgets it.
  **Compile Region** writes the region beside the map as `<name>-region.map`, unsaved edits included, and compiles
  it with the chosen profile, so `<name>-region` can be loaded in the game to test that area quickly. The region
  map keeps what touches the box, sealed by six brushes just outside it, and gets a player start (where the camera
  stands, or in the middle) if it has none. **Save Region As…** writes the same map anywhere you choose. **Run
  Profile** still builds the whole map.
- **Hide and show.** **Hide Selection** temporarily hides selected objects; **Show All Hidden** clears temporary
  hiding, and **Isolate Selection** hides everything outside the selected objects. All plan panes and the camera
  share this state. Hidden objects stay in the map, its builds and its packages. See [Temporary visibility](#temporary-visibility)
  for profile shortcuts, undo and the separate scene/filter controls.
- **Export Image** writes an SVG picture of the map for reviews and documentation.

## Temporary visibility

**Hide Selection**, **Show All Hidden** and **Isolate Selection** use session
visibility. NRC and default controls use <kbd>H</kbd> and <kbd>Shift</kbd>+<kbd>H</kbd>
for Hide and Show All. TB uses <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>I</kbd>,
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>I</kbd> and <kbd>Ctrl</kbd>+<kbd>I</kbd>
for Hide, Show All and Isolate respectively. The context menu follows your
current profile and shortcut overrides.

Hiding clears the object selection. Closed groups hide as a unit; isolating a
brush entity keeps its children, while isolating an individual child keeps its
owner visible and hides its siblings. Isolate considers the whole map, including
locked objects and objects outside the open group. Selecting a hidden object
in the **Objects** list reveals that object without revealing hidden siblings
or ancestors; selecting an entity preserves independently hidden children.
Ordinary plan and camera picks preserve hidden members of a closed group.
Hiding a Doom sector or
linedef also suppresses geometry that depends entirely on hidden records;
isolating a line retains its required vertices and sector references. Isolating
a Doom vertex also retains its incident lines, their endpoints and sector references.

The TB profile records each operation, including selection and object-list
reveal, as one undo step without marking the map modified. NRC and default
profiles keep visibility outside undo. Structural undo/redo tracks surviving
hidden object identities, including compacted Doom record IDs. Operations run
in a cancellable worker; cancellation or a changed document leaves the live
visibility untouched. Finish component, clipping, drawing and surface tools
before using these object commands.

**Show All Hidden** clears temporary hiding only. It leaves type/material/region
filters and the **Scene** tab's saved visibility check boxes in place. Toggle
those controls separately when an object remains invisible. Saving, compiling
and packaging include temporarily hidden objects; reopening starts a new
visibility session. The CLI's `map visibility` command returns the same session
result as selectors without writing a map.

The TB profile hides closed groups as containers. Existing objects moved into
one inherit its temporary hiding; newly authored objects are explicitly shown,
so drawing in a hidden destination does not make the result disappear. Revealing
a child in an open group through the **Objects** list leaves its container hidden.
A closed group remains one selection unit, so row activation reveals its members. Hiding the container
again clears these shown overrides, preserving independently hidden children.
Nested groups and empty descendant groups participate in isolation. **Show All
Hidden** resets both hidden and shown overrides. Undo restores exact states and
selection; deleting and restoring an object or group preserves its identity.
NRC and default profiles hide the current group members as individual objects.
Existing session states retain their meaning when switching profiles.

Moving a group or assigning objects to another layer resets their shown
exceptions, including descendants that inherit their owner's membership.
Moves within a layer preserve them. Undo restores visibility and selection in
the same step; later transient hiding is retained. In TB and default profiles,
**Duplicate**, **Array** and **Create Linked Duplicate** preserve explicit hidden/shown attributes. The inserted
root is revealed while independently hidden descendants stay hidden. Linked
updates rebuild fresh inherited children, so source visibility exceptions do
not spread to other linked groups. New empty groups in hidden destinations also
receive a shown exception. Portable map/clipboard data does not store these
session attributes.

The NRC profile gives new objects and recursive copies fresh inherited temporary
visibility. A hidden source descendant becomes visible in its copy unless the
destination parent still hides it. New geometry, paste and empty groups also
inherit a hidden destination. The profile's **New and duplicated objects** row
in the controls reference describes the policy. Changing profiles affects future
authoring; it preserves existing states and recorded undo/redo results. A pending
placement prepared under a previous policy must be reopened.

Status: Partial. Saved scene visibility still takes precedence over temporary
shown overrides. Direct empty-group hide/show controls, component-mode hiding,
native gestures, full saved-layer/filter equivalence and real-project acceptance remain open.
NRC's automatic hiding of a brush entity after its final child is hidden also
needs parity work: VibeStudio currently derives the hidden label from its children.

## Select and inspect objects

The compact **Create**, **Select**, **Transform**, **Geometry** and **Surfaces** menus above the views bring the
authoring tools together. They use the same commands, profile shortcuts and selection checks as the main menus.
For example, **Transform** > **Duplicate with Offset…** prepares repeated objects, while **Geometry** offers
clipping, hollowing, component editing and patch tools. Unsupported actions are disabled for the current map or selection.

- Click objects in a view or in the **Objects** list on the **Outliner** tab; <kbd>Ctrl</kbd>+click and <kbd>Shift</kbd>+click build a
  multiple selection in either place. In a 2D view, <kbd>Tab</kbd> selects the next object, and
  <kbd>Ctrl</kbd>+<kbd>Tab</kbd> and <kbd>Shift</kbd>+<kbd>Tab</kbd> add the next or previous one. In a Doom map, a
  click inside a room selects its sector.
- Type in the **Objects** filter to narrow the list by name, or test properties with `key=value`, `key:text`,
  `key!=value`, `key<n` and `key>n`, such as `class=light`, `tag=3`, `texture:brick` or `light>200`. Every term must
  match. <kbd>Enter</kbd> selects everything the filter keeps, and <kbd>Down</kbd> lists recent queries.
- **Select All** (<kbd>Ctrl</kbd>+<kbd>A</kbd>) selects every entity, brush and patch that is not hidden in a
  Quake-family map (never `worldspawn` itself), and every thing in a Doom map; Doom vertices, lines and sectors are
  picked by hand. **Invert Selection** (<kbd>Ctrl</kbd>+<kbd>I</kbd>) works within the same set, and **Select None**
  (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>A</kbd>) clears the selection.
- The **Edit** menu adds **Select All of This Class**, **Select by Texture…**, **Select Targets**, **Select Sources**,
  **Expand Selection to Whole Entities** (the whole brush entity of every selected brush or patch, as Radiant's
  Expand Selection to Entities does) and, for Doom maps, **Select Connected Geometry**.
- **Select Siblings** and **Select by Source Line Number…** find Quake-family objects by their parents or their
  position in the loaded map text. Both are in **Select**, **Tools > Select** and command search. See
  [Select by hierarchy or source line](#select-by-hierarchy-or-source-line).
- **Select Connected Entities** adds direct incoming and outgoing entity connections to the current selection.
  NRC binds **Ctrl+Shift+E**. See [Select connected entities](#select-connected-entities).
- **Select All of Type** selects every selected owner's class, or uses the current material when no entity class
  is selected. In face mode it selects matching faces. NRC binds **Shift+A**. See [Select all of a type](#select-all-of-a-type).
- **Select by Region**, in the right-click menu and the **Select** menu above the views, follows the active editor
  profile. TB uses each brush's convex volume and removes the selection brushes; NRC uses each brush's bounds,
  removing selected objects for **Inside** and keeping them for **Touching**. The default profile uses combined
  bounds and retains geometry. See [Select by brush volume](#select-by-brush-volume) before using a consuming command.
- The **Transform** section at the top of the **Inspector** tab shows where the selection's centre is and how big it
  is. Type a new centre to move it there, or a new size to resize it, keeping its lower corner; point entities
  move but have no size to change. Narrow sidebars and larger text stack the axis fields so the complete numeric
  range stays readable. Widen the sidebar to restore the compact matrix. Reflow preserves pending edits and focus;
  the tab order is Centre X/Y/Z, then Size X/Y/Z in either layout. If a pane is narrower than a native control's
  minimum size, a horizontal scrollbar keeps its contents reachable.
- The **Inspector** tab lists the selection's keys and values. Double-click a value, or press <kbd>Enter</kbd>, to
  edit it; colour keys open a colour picker and spawnflags show as check boxes. Right-click a row for **Find Objects
  With This Value** or **Copy Value**. A selected brush lists its faces with texture, shift, rotation and scale; click a
  face in the 3D view to jump to it.
- Right-click an object in a 2D view or the **Objects** list for the map actions menu. **Copy Selector** copies its
  name, such as `entity:3`, for the command line.

## Edit Quake-family maps

Most actions are in the **Edit** menu, the right-click menu of the views and the **Objects** list, and command search
(<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd>). Each edit is one undo step.

| To | Do this |
| --- | --- |
| Add an entity | Choose **Add Entity…**, or drag a class from the **Entities** tab onto a 2D view (<kbd>Enter</kbd> or **Place in View** places it in the middle of the view). The new entity is selected, so its keys are on the **Inspector** at once. The classes come from your [entity definitions](#check-maps-and-entities), or from the built-in classes of Quake, Quake II or Quake III Arena, each marked with its editor colour. |
| Add a brush | Choose a shape on the [**Shapes** tab](#build-with-shapes) and drag over empty space in a 2D view (profiles such as TrenchBroom and the Radiant family), or in the 3D view with **Draw Brush** on, dragging a footprint and using the wheel for depth. **Add Brush…** previews a box, wedge, cylinder, cone or sphere first. |
| Make a brush entity | Select brushes and choose a class under **Brush Entities** on the **Entities** tab, or **Brush Entity** > **Tie to Entity…** in the right-click menu, as in Hammer and Radiant. With no brushes selected, the class arrives as a new brush where you drop it. **Move to World** gives an entity's brushes back to the world; an entity left with none goes. |
| Create an entity by class | Every point and brush class of your entity definitions has its own **Create** command, such as **Create light** or **Create func_door**, as in TrenchBroom: type the class in command search, or give it keys in **Help** > **Keyboard Shortcuts** (no profile gives them keys, as TrenchBroom does not). A point class goes in at the pointer in the view you used last, or in the middle of that view, its definition box standing on the brush surface or plane there and lined up with the grid, as **Paste at Cursor** places objects. A brush class makes the selected brushes and patches, with those of selected brush entities, into one, so it waits until brushes are selected; the **Brush Entity** menu uses the same commands and shows their keys. **Create Entity…** (**Edit** > **Brush Entities**, the **Create** menu above the views, or the **Tools** tab) lets you type a class instead. The commands follow the loaded definitions, and keys you give a class are kept while a map without it is open. Each is one undo step; a locked or hidden brush, one outside the open group, or a locked current layer refuses it with the reason. |
| Move brushes between entities | Select the brushes and patches, then an item of the entity that should take them, and choose **Move Primitives to Entity** (<kbd>Ctrl</kbd>+<kbd>M</kbd> in the NetRadiant Custom profile): the entity of the item you picked last takes the others and keeps its keys, as NetRadiant Custom does. **Move Primitives to First Entity** uses the item you picked first. **Move Selected to World** gives only the selected brushes and patches back to the world, while their entities keep the rest and their keys. An entity left empty goes, hidden brushes of a selected entity stay where they are, and locked or linked objects refuse the move. Brushes keep their numbers, groups and visibility; one leaving an entity for the world joins its entity's group, or else the open group or current layer. |
| Explode an entity | **Explode Entity** gives every brush and patch of the selected brush entities an entity of its own with the same keys, as bobToolz does. The entity keeps its first brush, and the new pieces are selected with it. **Explode Entity, Unique Names** gives each new piece its own `targetname`, so triggers still fire only the original. Worldspawn, locked and linked entities are refused. |
| Place models and sounds | The **Models** and **Sounds** tabs list the open package's models and sounds, with a preview of the chosen model and a waveform you can play. Drag one onto a view, or choose **Place in View**, for a `misc_model` or a speaker entity; **Give to Selection** sets the selected entities' `model` or `noise` key instead. The **Place** section names the class it makes in the open map, and says so when your definitions do not declare it: Quake itself has no model or speaker entity, so a Quake mod must provide them. |
| Duplicate or delete | **Duplicate Selection** copies objects using your profile's placement; **Duplicate with Offset…** previews an exact offset and a **Copies** count for a linear array. <kbd>Delete</kbd> removes the selection; an entity takes its brushes with it. |
| Move | Drag the selection, nudge it one grid step with the keys your profile gives the **Nudge** commands (see [Nudge the selection](#nudge-the-selection)), or use **Move Selection…**. |
| Rotate or flip | **Rotate 90° Left**, **Rotate 90° Right**, **Flip Horizontal** and **Flip Vertical** work as the active view shows the map, the camera included, and **Roll** and **Pitch** turn about the view's other axes; see [Turn and flip from the view](#turn-and-flip-from-the-view). **Flip X**, **Flip Y**, **Flip Z**, **Rotate X**, **Rotate Y** and **Rotate Z** (**Edit** > **Transform**, the **Transform** menu above the views, or the **Tools** tab) use the world axis whatever view is active, as NetRadiant Custom's commands do: Rotate X and Rotate Z turn -90° and Rotate Y +90°, each clockwise as the side, top or front view shows it. Doom maps flip along X or Y and turn about Z only. No profile gives them keys, as NetRadiant Custom does not; add your own in **Help** > **Keyboard Shortcuts**. In the NetRadiant Custom and TrenchBroom profiles, every quick turn pivots on the grid point nearest the selection's centre while **Snap** is on, so a brush whose sides differ by an odd number of grid steps stays on the grid; it may move by up to half a step. Other profiles turn about the exact centre; **Customize Editor Gestures** changes this for any profile, and a custom component pivot always wins. Flips mirror about the exact centre, except in the TrenchBroom profile, which rounds it to half the grid while **Snap** is on. **Rotate Selection…** turns by any X, Y and Z angles about the selection centre, the world origin or your own pivot; see [Rotate and scale by exact values](#rotate-and-scale-by-exact-values). |
| Align | **Transform** > **Align Left**, **Align Right**, **Align Top**, **Align Bottom**, **Centre Horizontally** and **Centre Vertically** line the selected objects up on the edge or centre of the whole selection, as the active 2D view shows it, as Hammer does. An entity moves with its brushes. |
| Shear | Turn on **Transform** > **Shear Tool** and drag the handle in the middle of a side of the selection in any 2D view: that side slides along itself and the opposite side stays put, slanting the selection, as TrenchBroom's shear tool does. <kbd>Escape</kbd> turns the tool off. **Transform** > **Shear…** slants the selection about its centre by an angle you type. Texture lock follows **Texture Lock**; where the map's face format cannot slant textures, the tool shears without it and says so. |
| Resize | Drag the handles in a 2D view, or the labelled X, Y and Z handles in the 3D view. **Resize Selection…** takes exact sizes and the point that stays in place. **Scale Selection…** multiplies the size by factors, and a negative factor mirrors; see [Rotate and scale by exact values](#rotate-and-scale-by-exact-values). |
| Cut with a plane | Turn on the **Clip Tool** (<kbd>X</kbd>), place or drag two or three points in any view, use <kbd>Tab</kbd> to choose sides, then <kbd>Enter</kbd> to apply the checked result. **Clip Selection…** accepts an axis plane or three exact XYZ points, with **Preview** before applying. |
| Hollow | **Hollow…** turns each selected brush into walls of the thickness you choose, one per face, overlapping at the edges. **Hollow Touching…** makes walls that meet without overlapping, and **Make Room** builds walls one grid step thick around the outside of each brush and removes it, so the brush becomes the room, as NetRadiant Custom does. See [Make a room with caulk and openings](#make-a-room-with-caulk-and-openings). |
| Carve (CSG subtract) | **Carve** cuts the selected brushes out of every brush they overlap. The carving brushes stay selected, ready to delete; hidden brushes are left alone. |
| Intersect (CSG) | **Intersect** replaces the selected brushes with the one brush where they all overlap, as TrenchBroom does; each face keeps the texture of the face it came from. |
| Make detail | **Make Detail** stops brushes sealing the map or splitting its visibility: in Quake II and Quake III maps it sets each face's detail flag, and in Quake maps it moves the brushes into a `func_detail` for VibeMap2, as ericw-tools expects. **Make Structural** undoes it, taking only the selected brushes out of a `func_detail`. In the TrenchBroom profile (<kbd>Alt</kbd>+<kbd>S</kbd>) it also moves the selected brushes out of any brush entity to the world, into the open group or current layer, as TrenchBroom does; other flag-based tags stay as they are. |
| Drop to the floor | **Drop to Floor** moves the selected point entities straight down onto the brush or patch below, keeping each class's height above it. |
| Round to the grid | **Round Selection to Grid** rounds every brush vertex, patch control point and point-entity origin of the selection onto the grid on its own, as Radiant's Snap To Grid does, so off-grid shapes are cleaned up where they stand; brush entities round their brushes and keep their origin key. In **Edit Vertices**, **Edit Edges** or **Edit Faces** it rounds only the picked components. It is one undo step, follows **Lock Component UVs**, and leaves a brush the grid would collapse as it was, saying why. The NetRadiant Custom profile runs it with <kbd>Ctrl</kbd>+<kbd>G</kbd>; **Snap Selection to Grid** instead moves each object whole so its corner or origin lands on the grid. In Doom-format maps it snaps objects as **Snap Selection to Grid** does. |
| Merge brushes | **Merge Brushes…** previews their convex union and lets you choose where conflicting materials, mappings and flags come from. |
| Apply a texture | Select brushes or patches and choose **Apply Texture…**, or press <kbd>Enter</kbd> on a tile on the **Textures** tab. **Find and Replace Textures…** finds or swaps one texture across the map, the selection or the picked faces, and **Reveal Material** shows the selection's texture on the **Textures** tab; see [Find, replace and reveal textures](#find-replace-and-reveal-textures). |
| Align textures | Edit a classic or Valve 220 face's shift, rotation and scale on the **Inspector** tab, or use the **Surfaces** tab. In Face mode, both targets use exactly the [picked face components](#edit-surfaces-from-component-faces). In object mode, **Target** chooses the selection, patches included, or **Picked Faces**: the faces you pick in the camera with your profile's face-pick click, holding its add key to gather more, or else the face in the **Inspector**. **Adjust** shifts, rotates, scales, fits or centres in steps, adds or subtracts a Radiant **Scale step** (see [Step textures by Radiant increments](#step-textures-by-radiant-increments)), and has one-click **Flip U** and **Flip V**, **Fit U** and **Fit V** (one repeat across that axis only), **Fit Width** and **Fit Height** (one repeat, keeping the texture's proportions), **Justify Left**, **Right**, **Top** and **Bottom**, **Natural** (the texture at its natural scale, square to the world, as Radiant's Natural does), **Align to Face** (the texture laid along the face's own slope), **Axial**, **2D View** and **Camera** under **Project texture** (see [Fit and project textures](#fit-and-project-textures)), and **Reset** and **Radiant Reset** (see [Reset textures](#reset-textures)). **Copy and Paste** carries one face's material, mapping and flags to others. **Surface Alignment…** gives a detailed preview, and **UV Editor…** drags one face's or patch's points over its texture (see [Edit texture coordinates in the UV Editor](#edit-texture-coordinates-in-the-uv-editor)). To move or flip textures the way the camera shows them, see [Move and flip textures from the camera](#move-and-flip-textures-from-the-camera). |
| Paint materials | Choose a material in the **Material** box above the views, set the camera tool to **Paint**, then click or drag across surfaces in the 3D view. Each stroke is one undo step. **Sample** picks up a surface's material instead. |
| Reshape a brush | Turn on **Edit Vertices**, **Edit Edges** or **Edit Faces** (**Geometry** or **Tools > Vertices**) to show corners, edge middles or face centres in the plan views and camera. Picks are shared between panes. In a plan view, click stacked handles, use the profile's add/toggle modifier or drag a selection band, then drag them or nudge them with your profile's **Nudge** keys (<kbd>Shift</kbd>: eight grid steps; see [Nudge the selection](#nudge-the-selection)). The camera supports visible/X-ray picks and XYZ movement; see [camera components](#move-components-in-the-camera). A corner moved onto another merges with it; **Undo** restores it. **Snap Vertices to Grid** rounds brush corners onto the grid. **Edit Brush Components…** provides exact numbers and a preview. |
| Make curves (Quake III) | **Add Patch…** creates a plane, cylinder, square cylinder, cone, sphere, bevel or end cap, its cylinders, cones and spheres true circles of as many spans as you choose; the **Shapes** tab draws them too. **Edit Patch Control Points…**, **Stitch Patches…** and **Cap Patch…** reshape, join and close patches, and **Edit Vertices** drags their control points in the plan views and camera. The **Patch Rows and Columns** commands, also in the patch editor, insert, add or delete rows and columns without changing the surface, **Transpose**, **Re-disperse** and **Smooth** them, as Radiant's Patch menu does, and **Thicken Patch…** gives patches a back and walls along their open edges, as NetRadiant Custom does. |
| Link entities | Pick the source objects first and the destination last, then choose **Connect Entities**. NRC requires exactly two objects; see the connection rules below. |
| Replace key values | **Edit** > **Replace Key Values…**, or the search button on the **Inspector**'s **Properties** section, finds a value of one key across the map's entities (or the selected ones) and replaces it: the whole value, or with **Match the whole value** off, every occurrence, such as a name's prefix. It counts the matches as you type, and the change is one undo step. |
| Colour lights and entities | Select lights or other entities, or brushes of a brush entity, and choose **Set Entity Colour…** (**Edit** > **Keys**, the right-click menu, or <kbd>K</kbd> in the NetRadiant Custom and NetRadiant profiles). The picker opens on the colour of the entity you picked last, or on the colour you chose last, and the choice is written to the `_color` key of every one of those entities as one undo step. A key already written as 0-255 stays 0-255; a new one is written as 0-1 fractions. **Normalise Entity Colour** scales the last-picked entity's colour so its brightest channel is 1 (black becomes white) and sets it on all of them, as NetRadiant Custom does. World brushes and hidden entities are left alone, and a locked entity refuses the change. |
| Reuse a group of objects | **Export Selection as Prefab…** or **Save Selection…** on the **Prefabs** tab saves a `.vprefab` file, and the tab lists it at once with those in the project and the open package. **Insert Prefab…** places one with a preview, or drag one from the tab onto a 2D view to place it where you drop it. **Replace Selection with Prefab…** puts one where the selection stands, its anchor at the selection's bottom centre, and removes the selection, as one undo step. |

TB and NRC duplicate at the original position by default. VibeStudio Default
moves copies one grid step right and down in the active plan. Change this per
profile in **Customize Gestures…** > **Editing** > **Nudge duplicated objects one
grid step right and down**. A command invoked from the camera uses the last active
plan's axes. **Profile default** restores the profile's own placement.

Held NRC surface-transfer gestures retain the surfaces under the original
camera view while their material preview changes. Picking at face edges follows
the same rule for the initial and subsequent hits, including surfaces cut by
the camera's near plane. Release commits the stroke as one undo step; **Cancel
Stroke** discards the full preview.

The NRC profile enables duplicate commands only in object mode; leave the
vertex, edge or face tool first. A duplicate remains one undo step. **Repeat**
uses its recorded offset even if you subsequently change the preference, and a
pending placement rejects changes to that preference before publication.

**Connect Entities** uses the order you pick objects. In the NRC profile
(**Ctrl+K**), select exactly two objects with different entity owners. Brushes
and patches use their owner, including worldspawn. The source's existing `target`
value takes priority and becomes the destination's `targetname`, even if that
replaces an existing name. Otherwise the destination's name is reused; if both
are empty, a name based on its classname is allocated against the map's names
and target references. Names preserve case and whitespace. Link display and source/target navigation
use exact native property keys, so differently cased custom keys cannot mask a
real connection. Other profiles keep
multi-source connections: the last distinct entity is the destination, its name
takes priority, and worldspawn is excluded.

**KillConnect Entities** (NRC: **Shift+K**) uses the same two-object rules with
`killtarget` instead of `target`. It leaves the source's ordinary target intact.
The action requires a Quake, Nexuiz or Xonotic game context. The current project's
game wins, then its selected installation, then the map's built-in game evidence;
Quake II, Quake III and Doom contexts disable it. Both commands are in **Edit**,
the level **Tools** tab, the context menu and command search. Inspect existing
connections before changing a destination name: other references are retained.

Each changed connection is one undo step. A redundant NRC connection succeeds
without adding history. Selection remains unchanged. Locked records and open
group boundaries are respected; linked groups share ordinary properties
while protected values stay local to their group. Preparation runs with cancellable
progress and rejects a pending result if the map, selection, profile or game
changes. Native-input and cross-platform acceptance remain **Partial**.

**Duplicate, Make Unique** (NRC: **Shift+Space**) copies selected objects into new
entity owners. Selecting part of a brush entity copies its properties and only
the selected brushes or patches; world brushes keep worldspawn as their owner.
Copied `targetname`, `target`, `killtarget` and numbered `target` properties are
renamed together using unused numeric suffixes. For example, a copied `gate`
target and its trigger both become `gate1` when that name is free. Outgoing
references are renamed too, even when their destination is outside the selection;
review those connections after copying. Other properties keep their values.
This command uses the same nudge preference, requires object mode and a Quake-family
text map, and has one undo step. **Repeat** allocates fresh names each time.

For repeated steps, columns or props, set **Copies** in **Duplicate with Offset…** to 1–256 additional copies.
With an X offset of 64, the first copy moves 64 units from the source, the second 128, and the third 192.
The preview shows every copy; **Place** selects them all in one undo step. Copies preserve their owners, textures and
scene memberships. The same workflow copies Doom/Hexen/UDMF things. UDMF copies retain fractional XYZ positions,
comments and unknown thing properties; geometry duplication is unavailable. Entity target
names retain ordinary duplicate behaviour. Use **Duplicate, Make Unique** for
NRC-style names, or **Insert Prefab…** for reusable assemblies. Automation can
combine `map duplicate --make-unique --copies N` with an explicit offset.
An invalid copy or cancellation leaves the entire map unchanged.

**Texture Lock** (on by default) keeps textures fixed to faces as you move, rotate and flip; **Texture Scale Lock**
stretches them when you resize. When exact locking needs explicit texture axes, the edit is refused unless **Allow Valve
220 Conversion** is ticked, which converts every classic face in the map in the same undo step. Your compiler must
support Valve 220.

<details>
<summary>Edits the editor refuses to protect your file</summary>

VibeStudio rewrites only the lines you change, so the rest of the file stays byte for byte. It refuses an edit it
cannot write back safely:

- deleting or copying an object whose brace shares a line with other text;
- changing a texture on a line that holds more than one face, or a texture name the map's syntax would split;
- moving, rotating, flipping, resizing or copying a brush that writes two faces on one line (clip, hollow and carve
  refuse it too);
- a number edit on a face whose numbers continue onto the next line.

A key whose value is written on the next line keeps its old value when you save.

</details>

### Nudge the selection

Use the **Nudge** commands to move the selection one grid step at a time. They are in **Edit** > **Nudge**, the
**Tools** tab and command search:

| Command | In a plan view | In the camera |
| --- | --- | --- |
| **Nudge Left**, **Nudge Right** | Left or right across the view | Along the camera's right axis, snapped to the nearest world axis |
| **Nudge Forward**, **Nudge Back** | Up or down the view | Along the horizontal world axis nearest the camera's view |
| **Nudge Depth Up**, **Nudge Depth Down** | Towards or away from you: Z in the top view, Y in the front view, X in the side view | Up or down world Z |
| **Nudge Up Along Z**, **Nudge Down Along Z** | Up or down world Z | Up or down world Z |

The keys come from your editor profile:

- **TrenchBroom**: the arrow keys, and <kbd>Page Up</kbd> and <kbd>Page Down</kbd> for depth, as TrenchBroom has them.
  In the single 3D layout the arrows nudge the camera's way, so you do not need to switch to a 2D view first.
- **NetRadiant Custom**, **GtkRadiant 1.6** and the other Radiant-family profiles, DoomEdit included, whose arrows
  drive the camera: <kbd>Alt</kbd>+arrow keys, and the keypad's <kbd>+</kbd> and <kbd>-</kbd> along world Z. From
  the camera they use the plan view you used last, as NetRadiant Custom does, and they come before **Go Back**,
  **Go Forward** and view zoom while a view has focus.
- **VibeStudio Default** and the other profiles: the arrow keys in the plan views. The default orbiting camera keeps
  the arrows for turning and panning, and so does any camera that flies or drives with them.

<kbd>Shift</kbd> with a nudge key moves eight grid steps, unless your profile gives that key another command, such
as the Radiant profiles' texture shifts. With nothing to nudge, the arrows pan the plan view as before. Give the
commands your own keys in **Help** > **Keyboard Shortcuts**.

A nudge moves the first of these that is active:

1. the sweep destination, while the **Sweep Tool** is on;
2. the shared component pivot, while **Rotate Components** or **Move Component Pivot** is chosen (this does not
   change the map, and **Reset Component Pivot** puts it back);
3. the picked vertices, edges, faces or patch points, and faces picked by **Select Textured**, in **Edit Vertices**,
   **Edit Edges** or **Edit Faces**;
4. the selected objects.

Each nudge of the map is one undo step. It follows **Texture Lock** for objects and **Lock Component UVs** for
components, and a locked object, or one outside the open group, refuses it with the reason. Split and
extrusion nudges stay in a plan view's plane. Doom maps nudge along X and Y only: there <kbd>Page Up</kbd> and
<kbd>Page Down</kbd> still raise and lower the selected sectors' floors, in the TrenchBroom profile too.

To nudge from a script, use `map nudge`, which resolves the same directions for a plan view or a camera; see
[the command line](cli.md).

### Copy and move in one step

Use the **Duplicate and Move** commands to copy the selection and move the copies one grid step, as one undo step.
They are in **Edit** > **Duplicate and Move**, the **Tools** tab and command search. **Duplicate and Move Left**,
**Right**, **Forward**, **Back**, **Depth Up** and **Depth Down** move the copies the way the matching nudge moves the
selection: across or up the plan view, along its depth axis, or along the camera's nearest axes. The copies are
selected afterwards, so pressing the key again copies the copies; **Repeat Last Commands** replays the copy and its
offset too.

- **TrenchBroom**: <kbd>Ctrl</kbd>+arrow keys, and <kbd>Ctrl</kbd>+<kbd>Page Up</kbd> and
  <kbd>Ctrl</kbd>+<kbd>Page Down</kbd> for depth, as TrenchBroom has them. The views take these keys only while a copy
  can be made: with nothing selected, <kbd>Ctrl</kbd>+arrow keys still pan a plan view, and on a Doom map
  <kbd>Ctrl</kbd>+<kbd>Page Up</kbd> and <kbd>Ctrl</kbd>+<kbd>Page Down</kbd> still raise and lower ceilings.
- The other profiles give them no keys. Add your own in **Help** > **Keyboard Shortcuts**.

The commands copy whole objects, so they are off in **Edit Vertices**, **Edit Edges** and **Edit Faces** and while the
**Sweep Tool** is on. They follow **Texture Lock** and the open group, and a locked object refuses them with the reason.
Doom maps copy things only, along X and Y. To copy and move from a script, add `--duplicate` to `map nudge`; see
[the command line](cli.md).

### Turn and flip from the view

Use the view-relative turns and flips to turn the selection a quarter turn, or mirror it, about the axes of the view you
work in. They are in **Edit** > **Transform**, the **Transform** menu above the views, the **Tools** tab and command
search:

| Command | In a plan view | In the camera |
| --- | --- | --- |
| **Roll Clockwise**, **Roll Counter-clockwise** | About the view's vertical axis: Y in the top view, Z in the front and side views | About the horizontal world axis nearest the camera's view |
| **Rotate 90° Right**, **Rotate 90° Left** (yaw) | About the axis that points at you, clockwise or counter-clockwise as the view shows it | About world Z, as seen from above |
| **Pitch Clockwise**, **Pitch Counter-clockwise** | About the view's horizontal axis | About the camera's right axis, snapped to the nearest world axis |
| **Flip Horizontal** | Left to right across the view | Along the camera's right axis, snapped to the nearest world axis |
| **Flip Vertical** | Top to bottom | Along world Z |

Clockwise means as you see it looking along the axis: forward for **Roll**, to the right for **Pitch**. The commands
use the view that has focus or that you used last; from the menus and command search, that is the last view you
clicked in. In the single 3D layout they always follow the camera. The NetRadiant Custom and other Radiant-family
profiles keep the last plan view's axes from the camera, as their nudges do.

The keys come from your editor profile:

- **TrenchBroom**: <kbd>Alt</kbd>+<kbd>Up</kbd> and <kbd>Alt</kbd>+<kbd>Down</kbd> roll,
  <kbd>Alt</kbd>+<kbd>Left</kbd> and <kbd>Alt</kbd>+<kbd>Right</kbd> yaw, <kbd>Alt</kbd>+<kbd>Page Up</kbd> and
  <kbd>Alt</kbd>+<kbd>Page Down</kbd> pitch, <kbd>Ctrl</kbd>+<kbd>F</kbd> flips horizontally and
  <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F</kbd> vertically, as TrenchBroom has them. The views take these keys only while
  the command can act; elsewhere, or with nothing selected, <kbd>Alt</kbd>+<kbd>Left</kbd> and
  <kbd>Alt</kbd>+<kbd>Right</kbd> still go back and forward and <kbd>Ctrl</kbd>+<kbd>F</kbd> still finds.
- **Hammer** and **J.A.C.K.**: <kbd>Ctrl</kbd>+<kbd>L</kbd> flips horizontally and <kbd>Ctrl</kbd>+<kbd>I</kbd>
  vertically. **J.A.C.K.** also turns with <kbd>Ctrl</kbd>+<kbd>R</kbd> (**Rotate 90° Left**) and
  <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>R</kbd> (**Rotate 90° Right**).
- The other profiles give them no keys, and only TrenchBroom binds **Roll** and **Pitch**. Add your own in **Help** >
  **Keyboard Shortcuts**.

A turn rotates the first of these that is active:

1. the sweep destination, while the **Sweep Tool** is on, by the rotation step of the component gizmo (15° unless you
   change it), as TrenchBroom turns its sweep cap by the grid angle;
2. the picked vertices, edges, faces or patch points, in **Edit Vertices**, **Edit Edges** or **Edit Faces**;
3. the selected objects.

Flips wait until you create or cancel the sweep. Each turn or flip of the map is one undo step that follows
**Texture Lock**, and a locked object, or one outside the open group, refuses it with the reason. Turns pivot as quick
turns do in your profile: on the grid point nearest the selection's centre in the TrenchBroom and NetRadiant Custom
profiles while **Snap** is on. The TrenchBroom profile flips about the centre rounded to half the grid, so a selection
that sits on the grid flips in place; set either policy for any profile in **Customize Editor Gestures** >
**Editing**. A custom component pivot always wins. Doom maps take **Rotate 90°** and the flips from the top view, but
not **Roll** or **Pitch**, which would tip the map out of its plane.

To turn or flip from a script, use `map view-transform`, which resolves the same axes for a plan view or a camera;
see [the command line](cli.md).

### Rotate and scale by exact values

Use **Rotate Selection…** and **Scale Selection…** (the **Edit** menu, the **Transform** menu above the views, the
right-click menu's **Transform** submenu, the **Tools** tab or command search) to type exact values, as NetRadiant Custom's **Arbitrary
Rotation** and **Arbitrary Scale** windows do. In the NetRadiant Custom profile they open with <kbd>Shift</kbd>+<kbd>R</kbd>
and <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd>; **Save Map As** keeps <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>S</kbd>, so
the keys do not clash. Other profiles give them no keys; add your own in **Help** > **Keyboard Shortcuts**.

1. Select the objects.
2. Open the window. **Rotate Selection…** takes **About X**, **About Y** and **About Z** angles in degrees and turns the
   selection about X, then Y, then Z, following the right-hand rule. It opens with 45° about the axis of the plan view
   you used last. **Scale Selection…** takes **X factor**, **Y factor** and **Z factor**: 2 doubles the size along that
   axis, 0.5 halves it, and a negative factor mirrors the selection along it. Brush faces and patches still face
   outward after a mirror, and entity and Doom thing headings reflect as **Flip** does.
3. Choose the **Pivot**: the **Selection centre**, the **World origin**, or **Custom coordinates** that you type.
4. Check the preview. **Lock textures to faces** (rotation) and **Stretch textures with the selection** (scale, the
   **Texture Scale Lock** setting) start from your settings; **Allow Valve 220 conversion** lets a locked texture that
   classic faces cannot hold convert the map's classic faces, keeping how they look.
5. Choose **Apply** to make the change and keep the window open, or **OK** to make it and close the window.

The windows do not block the rest of the studio. After **Apply**, and whenever you change the selection or the map, the
preview rebuilds from the current selection, so pressing **Apply** again turns or scales by the same amount once more.
Each **Apply** is one undo step, and **Repeat Transforms** repeats the whole X, Y and Z turn or the factors. A locked
object, or one outside the open group, refuses the change with the reason, and hidden objects stay where they are. Doom
maps turn about Z only and scale in the top view, so their other fields are off. In **Edit Vertices**, **Edit Edges** or
**Edit Faces** the windows turn or scale the picked components instead, about the pivot you choose or your component
pivot, following **Lock Component UVs** rather than the texture options; with nothing picked they wait and say why.

To do the same from a script, use `map rotate --euler x,y,z` or `map resize --factors x,y,z` with an optional
`--pivot`; see [the command line](cli.md). This workflow is **Partial**: it is tested in offscreen and command-line
checks, not yet with native input on macOS or Linux or in long real-project sessions.

### Repeat object commands

Use **Edit > Repeat > Repeat Last Commands**, **Tools > Transform**, or the command
palette to replay your recorded object moves, rotations, resizing, shears, mirrors
and ordinary duplications on the current selection. The TrenchBroom profile uses
**Ctrl+R**; **Ctrl+Shift+R** clears the sequence. The NetRadiant Custom profile
gives **Ctrl+R** to **Repeat Transforms** instead (see below). Other profiles leave
these keys unassigned so their existing compiler and editor bindings stay available.

Consecutive commands form a sequence. Changing the selection keeps it available
to repeat, but your next new transform or duplication starts a new sequence.
World pivots and transform origins stay where they were recorded; selecting a
different object does not recentre them. Replay uses the current **Texture Lock**
setting. It affects whole objects, including when component mode is active.

Each replay is one undo step, with progress and cancellation. Clear leaves the map
and its undo history intact; Undo and Redo also clear the repeat sequence. Save As
retains it, while reopening starts a new sequence. Component edits, textures, CSG,
clipping, snapping, prefab insertion and arrays with multiple copies are not
recorded. **Copy Repeat Recipe** exports JSON for the
[`map repeat` CLI workflow](../LEVEL_REPEAT.md#automation-and-studio-integration).
This workflow is **Partial**, pending wider real-project and native acceptance.

### Repeat the last transforms

Use **Repeat Transforms** (**Edit** > **Repeat**, **Tools** > **Transform**, or <kbd>Ctrl</kbd>+<kbd>R</kbd> in the
NetRadiant Custom profile) to build an array the way NetRadiant Custom does. VibeStudio remembers the last move,
rotation, scale (a resize or mirror) and shear you made, each kept until you make another of its kind, whether on
whole objects or on picked vertices, edges and faces. With objects selected, **Repeat Transforms** copies them where
they stand, then scales, shears and turns the copies about their centre and moves them, as one undo step. The copies
stay selected, so each press adds the next step of the array. In **Edit Vertices**, **Edit Edges** or **Edit Faces**
it transforms the picked components instead, about their centre or your component pivot, without copying.

The recorded transforms stay when you change the selection or undo; the status bar shows what was repeated, and the
**History** panel names it. **Reset Repeat Transforms** forgets them without changing the map. Snapping, duplicates and
repeats themselves record nothing. **Repeat Transforms** works on Quake-family maps; on a Doom map, use **Repeat Last
Commands**. To repeat transforms from a script, use `map repeat-transforms`; see [the command line](cli.md). This
workflow is **Partial**: it is tested in offscreen and command-line checks, not yet in long real-project sessions.

### Clip brushes across views

**Clip Tool** shares its numbered points between every plan and camera view.
Two points define a plane along the originating plan's hidden axis. Most camera
profiles use a vertical plane, with a horizontal fallback for a vertical line.
The NetRadiant Custom profile uses the camera policy described below. A third
point defines a sloped plane. Drag a numbered point to adjust it; switching
panes, projections or layouts preserves the plane. **Backspace** removes the
last point; **Escape** cancels a point drag, then leaves clipping.

New camera points land on visible brush faces. Grid snapping stays on the
face; near a boundary that falls outside the grid, the actual face hit is kept.
By default, double-click a brush face to use its exact source plane;
**Alt+double-click** always provides this action. Plan views snap the
two visible coordinates and preserve the point's hidden coordinate.
**Clip Selection… > Three points** provides labelled XYZ fields for numeric
and keyboard editing; **Preview** closes the dialog and shows the draft.

In the NRC profile, placing the second camera point chooses the dominant view
axis. A segment parallel to that axis uses the points' projected screen direction
to find a usable plane. If the plane would only touch an axial outer face of the
selection, its extrusion changes to cut into the brush. The chosen axis and sign
are stored with the actual points and shared by every view; simply moving the
camera never rotates an established plane. Dragging a point can repair a parallel
or outer-face plane. Cancelling the drag restores both the point and its plane.

**Controls > Customize Gestures… > 3D Camera** exposes **Two-point clip plane
follows the camera with outer-face correction** (`camera.clipViewAxis`). It is
on by default only for NRC; other profiles retain their previous behaviour.
The setting can be changed or exported with the other per-profile gestures.
Three real points and exact face matching are independent of this policy.
Overlapping points along the viewing ray need a sideways move or a third point.

For NRC camera point movement, press a numbered point without modifiers, then
hold **Shift** to move along the strongest XY axis or **Alt** to move along Z.
**Alt+Shift** stays vertical. Movement snaps relative to the point's starting
position, preserving any off-grid offset; exact half-grid ties round to the
nearest even grid step. The press anchor stays fixed when changing modifiers.
Hold **Ctrl**, including Ctrl+Shift or Ctrl+Alt, to return to visible brush-face
snapping. Releasing Shift/Alt also returns to surface picking; a miss retains
the previous point. The movement axis and any refusal appear in the readout.

The same **Customize Gestures… > 3D Camera** form offers **Clip-point movement**
(`camera.clipDragStyle`): `world` is NRC's default, `view` uses the axial plane
nearest the camera view with Alt along its depth axis, and `surface` keeps
surface-only dragging. Other profiles default to `surface`. This setting is
independent of the two-point plane policy. Start a drag without modifiers;
modified initial presses retain the profile's other selection/navigation roles.
VibeStudio's plain first-point drag can still add a second point; modified
movement instead moves the current point. Wider scene-edge/vertex/patch snapping
remains incomplete. **Clip Selection…** supplies numeric XYZ alternatives.
Cancelling, changing the view/grid/profile, or replacing the shared plane
discards an unfinished point drag. Applying the checked cut creates one undo step.

**Tab** cycles back, front and both. **Ctrl+Enter** flips the side;
**Shift+Enter** splits into both pieces. **Enter** waits for validation, then
applies its exact result in one undo step. **Checking cut…**, **Checked cut**
and **Cut refused** report the state. Dashed outlines show the kept result;
the original surfaces remain visible. Plan hatching marks the discarded side
at the first point's depth; use the wire result to inspect an oblique cut.

Brushes wholly on the discarded side are removed, including an emptied brush
entity. Untouched brushes remain selected. Patches and point entities are not
cut. Invalid geometry, a protected object or unsafe source text refuses the
whole operation. New cut faces inherit the brush's most-used material and flags
by default, with the native format's default projection. **Clipper Options…**
can use the **Current material** or a **Custom / caulk material** instead.
Choose a material provided by the project's game assets; `common/caulk` is an
editable example, not a universal game resource. Explicit materials preserve
brush contents but start with zero surface flags and value. Existing faces
retain their source text. Changing the current cut material rechecks the plane
and cancels a queued Apply. A map, selection or save-context change cancels an obsolete draft.
Undo restores geometry, owners, scene membership and selection.

**Quick Clip** starts a single cut and exits after it succeeds. A refused cut
keeps the tool and points available for correction. The NetRadiant Custom
profile also starts quick clipping with **Ctrl+left-click/drag** in a plan view
while brushes are selected and no other drawing/component tool is active.
Remap or disable it using **Mouse and camera controls > Quick clip with left
button** (`plan.quickClipModifiers`). Existing custom selection bindings keep
their priority; explicit conflicting choices are rejected.

**Clipper Options…** saves these preferences across sessions and profiles:

| Option | Behaviour |
| --- | --- |
| **Clear points after a successful cut** | On by default. Turn off to keep the plane for adjustment; undo and selection changes still retire it. |
| **Reset to Back** | Off by default. Reset the kept side after successful cuts and when entering the tool, independently of retaining points. |
| **Use two points in plan views** | Off by default. A click away from the handles starts a fresh plane after two points; cancelling the drag restores the previous plane. Camera and numeric editing still accept three. With three points, a further click away starts a new plane in either view. |
| **Double-click** | Choose no cut (default), Clip or Split. Double-click a numbered point to apply the checked result; Alt+double-click in the camera still matches a face. |
| **Wrap/merge when the plane makes no cut** | Off by default. One point, a degenerate plane, or a plane that neither cuts nor removes anything builds a convex hull around the selected brushes and actual placed points. This fills gaps and concavities. |

Opening the options dialog cancels a queued Apply, including when the dialog is
subsequently cancelled. **Clip Selection…** shows the cut material and links to
the same options. Quick clipping always exits on success, regardless of reset
preferences. These settings work locally with AI disabled.

With **Wrap/merge** enabled, the dashed preview explicitly names the hull
operation. New hull faces use the **current material**, independently of the
cut/caulk setting. Exterior source planes retain their materials, UVs and flags;
when several faces support the same plane, the first in selection order supplies
its surface. The last selected brush keeps its identity, owner and scene group.
Empty source brush entities are removed. Undo restores all sources and selection,
and linked groups update through the usual scene service. A failed or locked
cut never falls back to a hull. An enclosed point on a single brush changes
nothing and is refused without adding history.

Use **Clip Selection… > One point (wrap/merge)** for numeric XYZ entry. Numeric
wrap/merge requires actual points rather than an axis/position plane. Up to 64
brushes in the same face format, 2,048 source faces and 4,096 input points are
supported; the hull must remain within ±32768 units, 128 faces and 256 vertices.
This clipper option wraps whole selected brushes. **Merge Brushes** still
requires an exact convex union and refuses gaps.

The wire preview has a 65,536-segment limit and announces truncation. The edit
still validates the full selection. Cuts use a 0.01-unit side tolerance; brushes
entirely within that tolerance refuse a one-sided cut. Native input and
screen-reader acceptance remain unverified.

The CLI uses the same atomic operation:

```sh
vibestudio --cli map clip maps/start.map --object brush:12 --axis x --at 64 --keep back --output maps/clipped.map
vibestudio --cli map clip maps/start.map --object entity:3 --points "0,0,0 64,64,0 0,0,64" --keep both --output maps/split.map
vibestudio --cli map clip maps/start.map --object brush:12 --axis x --at 64 --cut-texture common/caulk --output maps/caulk-cut.map
vibestudio --cli editor clipper --set material=custom --set customTexture=common/caulk --set resetSide=on --dry-run --json
vibestudio --cli map clip maps/start.map --object brush:12 --points "128,0,64" --wrap-merge --wrap-texture stone/wall --output maps/wrapped.map --json
vibestudio --cli editor clipper --set wrapMerge=on --dry-run --json
vibestudio --cli editor gestures netradiant-custom --set camera.clipDragStyle=view --dry-run --json
vibestudio --cli map clip maps/start.map --object brush:12 --points "0,-48,80 0,48,80" --camera-eye "-200,0,120" --camera-forward "1,0,0" --keep both --output maps/camera-split.map --json
vibestudio --cli map clip maps/start.map --object brush:12 --points "0,-48,80 0,48,80" --view-axis z --view-sign 1 --keep both --output maps/replayed-split.map --json
```

Repeat **--object** for several selections. Choose one plane definition.
Unknown, repeated and incomplete options fail before saving; **--dry-run**
prepares the edit without writing an output. The `clipped` JSON count includes
whole brushes removed; `pieces` contains the remaining selection. An empty
`cutTexture` JSON field means inherited material. **map clip** defaults to
inheritance independently of GUI preferences; **--cut-texture** explicitly
chooses the output material for reproducible scripts.

**--wrap-merge** requires **--wrap-texture** and one to three actual **--points**.
For two points, **--view-axis x|y|z** chooses the plane's extrusion axis (default
z); that synthetic third point never joins the hull. A valid cut or whole-side
removal takes precedence. JSON `operation` is `clip` or `wrap-merge`; `changed`
counts source brushes, `clipped` is zero for a wrap, and `inheritedFaces`,
`newFaces` and `wrapTexture` describe its hull surfaces.

Two-point cuts without wrapping require either **--view-axis x|y|z** or both
**--camera-eye x,y,z** and **--camera-forward x,y,z**. The camera options use the
same NRC policy and selected bounds as the GUI; the forward vector need not be
normalised. **--view-sign 1|-1** requires **--view-axis** and defaults to `1`.
Do not combine explicit axis/sign and camera options. Two-point JSON reports
`planeAxis`, `planeDirection` and `cameraPlane`; the first two can replay the
result without a camera. The synthetic third point still never joins wrap/merge.

### Make a room with caulk and openings

**Status: Partial.** Draw a brush the size of the room you want, select it and choose **Make Room**
(**Geometry** menu, or the **Brush** section of the **Tools** tab). VibeStudio
builds a wall one grid step thick outside each face and removes the brush, so
its volume becomes the room. The walls fill the outer corners, where NetRadiant
Custom leaves them open. It is one undo step.

As in NetRadiant Custom, two options start on:

- **Caulk the wall faces the room does not see**: every wall face except the
  one facing the room takes the game's caulk material, so the compiler leaves
  it out. That is `common/caulk` in Quake III and `skip` in Quake, the same
  faces the **Caulk** filter hides. Quake II has no caulk material, so the walls
  keep their textures until you name one.
- **Leave picked faces open**: faces you pick in face mode (**Edit Faces**), in
  the camera or with **Select Textured** get no wall, so they become openings
  such as doorways or a skylight. The walls beside an opening stop flush with
  it, and the wall faces that line it keep the open face's material.

To change them, choose **Make Room Options…**. Leave **Caulk material** empty
for the game's own caulk, or name a material your game's assets provide. The
line under the options shows which material Make Room will use on the open map.
**Restore Defaults** brings back NetRadiant Custom's behaviour. The options are
saved with your settings, and `editor make-room` reads or changes them from
the command line.

From the command line, `map hollow --mode room` does the same. Add `--caulk`
for the game's caulk or `--caulk-texture NAME` for your own, and repeat
`--open-face face:brushId:faceNumber` (one-based, as `map select-textured`
prints faces) for each opening. The same options also work with
`--mode overlap` and `--mode touch`:

```text
vibestudio --cli map hollow maps/start.map --object brush:12 --mode room --thickness 16 --caulk --open-face face:12:6 --output maps/start-room.map --json
```

### Hollow, shrink and expand with the CSG Tool

The **CSG Tool** section of the **Tools** tab, after **Brush**, works like
NetRadiant Custom's CSG Tool window. Choose **CSG Tool** (**Geometry** menu or
the command palette) to open the section with the thickness ready to type. Set
the options once; every button, menu item and key then uses them, and they are
saved with your settings.

- **Thickness**: how thick the walls are, and how far **Shrink Brushes** and
  **Expand Brushes** move each face. **Use Grid** copies the grid size.
- **Faces**: which faces of each brush the tool leaves alone. **All but the
  picked faces** skips the faces you pick in face mode, in the camera or with
  **Select Textured**. **Only the picked faces** works on those alone; a brush
  with none picked is left as it is. **All but those facing the 2D view** skips
  the faces at either end along the active 2D view's axis (in the top view, the
  floor and ceiling), and **All but those facing the camera** does the same
  along the 3D camera's view, so hollowing makes a shaft or a passage.
- **Caulk hidden faces**: hidden faces take the material **Make Room Options…**
  names, or the game's own caulk. The line under the options shows which.
- **Keep inner brush**: keeps the brush instead of removing it. The room is
  then solid, so caulk goes on the faces between the walls and the brush, and
  the outside keeps its materials.

Select brushes, or a brush entity, then choose one of these. Each is one undo
step; a skipped face gets no wall, or stays where it is, and the walls beside
it stop flush with it.

- **Shrink Brushes** and **Expand Brushes** move every face in or out by the
  thickness. Textures stay where they are in the world. A brush a shrink would
  flatten is left as it was, and the status bar says why.
- **Hollow with Diagonal Joints** builds the walls outside each brush, so the
  brush becomes the room. Neighbouring walls meet on a mitred joint, with no
  overlap and no gap.
- **Hollow by Wrapping** builds the walls inside each brush, each stopping at
  the walls before it, like **Hollow Touching…**.
- **Hollow by Extruding Faces** builds a wall straight out from each face, its
  sides square to the face, so the outer edges stay open.
- **Hollow by Pulling Faces** builds the walls outside with the corners filled,
  like **Make Room**.

Hidden brushes, and brushes in locked layers or groups, are left as they are.
From the command line, `map hollow` takes `--mode diagonal`, `--mode extrude`,
`--mode wrap` (the same as `touch`) and `--mode pull` (the same as `room`), with
`--keep-inner`. `--exclude-face face:brushId:faceNumber` and `--only-face`
choose faces as **Faces** does with picked faces, and `--exclude-facing x`,
`y`, `z` or a direction such as `0.7,0.7,0` skips the faces facing it.
`map shrink-brushes` and `map expand-brushes` take the same face options with
`--thickness`:

```text
vibestudio --cli map hollow maps/start.map --object brush:12 --mode diagonal --thickness 16 --exclude-facing z --caulk --output maps/start-shaft.map --json
vibestudio --cli map expand-brushes maps/start.map --object brush:12 --thickness 8 --exclude-face face:12:6 --output maps/start-wide.map --json
```

### Wrap brushes or picked components

**Wrap/Merge** in **Geometry**, the selection context menu and **Tools > Brush**
creates a convex hull directly. The NetRadiant Custom profile assigns
<kbd>Ctrl</kbd>+<kbd>U</kbd>; other profiles can assign a command key. In object
mode, it replaces selected brushes, fills gaps and retains supporting exterior
surfaces as described above. If the clipper is active, its actual points also
join the hull. The clipper's optional fallback setting does not gate this command.

In **Edit Vertices**, **Edit Edges** or **Edit Faces**, the command wraps only
picked components. Edges contribute both ends, faces contribute their corners,
and patches contribute picked controls. Original brushes, patches and component
picks remain; the new brush joins the object selection. Every new face uses the
current material with default UVs and zero contents/surface flags. No source
surface projection is inherited. Flat or insufficient picks are refused; they
never silently expand to the whole source brush.

The new brush uses the last selected object's native entity and scene group.
With no object selection it follows the last component set; a selected point
entity falls back to worldspawn and the current creation context. Locked groups,
editing contexts and linked groups use the normal scene rules. One undo removes
the new hull from every linked group while restoring the previous selection.

**Wrap/Merge with Points…** adds a preview and up to three exact XYZ points.
Choose **Source objects** or **Wrapped brush** to compare geometry, and change
the new face material before applying. Geometry and available package/staging
materials load on a cancellable worker. Invalid inputs clear the old preview
and disable **Apply Wrap/Merge**. Closing the dialog cancels preparation; changes
to the map, selection, clipper points or package context refuse stale results.

Component wrapping accepts up to 256 component sets, 64 source objects and
4,096 input points, including extra points. The resulting hull has the same
128-face, 256-vertex and ±32768-unit limits as whole-brush wrapping. It remains
a convex brush: concavities and interior points do not become separate geometry.

```sh
vibestudio --cli map wrap-merge maps/start.map --object brush:12 --object brush:13 --texture stone/wall --output maps/wrapped.map --json
vibestudio --cli map wrap-merge maps/start.map --set brush:12/face/4 --point "128,0,192" --texture stone/wall --output maps/component-hull.map --json
vibestudio --cli map wrap-merge maps/start.map --set patch:2/vertex/0,2,4,6,8 --texture stone/wall --output maps/patch-hull.map --dry-run --json
```

Repeat **--object** or **--set**, but do not mix them. Component selectors are
**brush:ID/vertex|edge|face/ID,ID** or **patch:ID/vertex/ID,ID**; IDs come from
**map brush-components** and the zero-based `controlPoints` array in
**map inspect --json**. Repeat **--point x,y,z** up to three times.
**--texture** and **--output** are required; **--dry-run** checks
without writing. JSON reports `sourcesRetained`, `brushId`, `sourceBrushes`,
`sourcePatches`, `vertices`, `faces`, `inheritedFaces` and `newFaces`.

### Move components in the camera

**Edit Vertices**, **Edit Edges** and **Edit Faces** also show handles in the
3D camera. They share picks with the plan views and keep a camera-only layout
in 3D. Selected patches show their control points in every component mode.

Click a handle to select it; use the profile's toggle modifier to add or remove
it. In component mode, the other Ctrl/Shift key adds without removing existing
picks; holding both subtracts. A profile with no toggle modifier uses Ctrl.
For custom chords, an exact configured toggle/add role takes precedence over
the combined-key subtraction alternative.
Drag a rectangle from empty space to select several handles. The TrenchBroom
profile toggles each enclosed handle with a plain lasso and adds with Ctrl;
other profiles replace with a plain lasso and toggle with their toggle key.
**Customize Editor Gestures…** can change this through **Plain component lasso
toggles; toggle modifier adds**, separately for plan and camera views.

In the camera, a click selects every available handle with the same solved
vertex, edge or face geometry. Edge endpoints and face corners must match;
crossing edges or different faces sharing a centre remain separate. This keeps
shared terrain corners together. Plan views select the handles stacked at that
screen position, including different depths. Patch control points can share a
corner with other selected geometry.
The camera picks visible handles by default. **X-ray Component Handles** in
**Geometry** or **Tools > Vertices** includes handles behind surfaces.
**Select All** (or **Ctrl+A**) selects all available handles of the selected
objects, including occluded ones. **Select None** clears component picks;
**Invert Selection** reverses them. These commands preserve the object
selection and component mode, create no geometry history, and cancel pending
edits. Locked/closed-group objects do not expose component handles.

To distinguish overlapping depths, hold **Alt** and scroll over a component
handle: wheel down steps farther, wheel up steps nearer, wrapping at either
end. **Tab** and **Shift+Tab** cycle forwards/backwards at the last component
pick or wheel position in the focused viewport. With no previous position,
they start at the first on-screen picked handle, or the first available handle
if none is picked. **Next/Previous Overlapping Component** in **Geometry >
Component Selection** and **Tools > Component Selection** provide the same
commands for keyboard customisation. Menus retain the last focused view.

Cycling replaces only handles in the small hit area and preserves picks
elsewhere. Coincident geometry stays together; equal-centre crossing edges
and different faces remain separate. If several depths are already picked,
forward cycling starts with the nearest and reverse cycling with the farthest.
The status and accessible description give the overlap number and XYZ position.
Starting a plan drag on a picked depth preserves that single-depth selection.
For plan ordering, near means higher Z in Top, lower Y in Front and higher X in
Side. Camera ordering follows its view direction; **X-ray Component Handles**
controls whether occluded handles participate. Handles behind the camera or
outside the view are excluded even with X-ray enabled.

**Customize Editor Gestures… > Cycle overlapping components with wheel (None
disables)** changes the chord independently for plans and camera. Plain wheel
navigation stays available. Fractional wheel motion accumulates to a notch;
moving to another hit area or changing the view resets it. A truncated handle
context refuses cycling. Wheel/key cycling waits until an active edit ends;
the named commands cancel a pending component edit first. These are VibeStudio
extensions; exact native TB/NRC input parity remains unverified.

Switching between **Edit Vertices**, **Edit Edges** and **Edit Faces** converts
the current picks. A picked face becomes its boundary edges or corners; a
picked edge becomes its endpoints. Converting upwards requires a complete
boundary: both endpoints for an edge, every corner or every boundary edge for
a face. Three sides of a quad do not select that face even though all four
corners are covered. Patch control-point picks survive every mode switch.
Choosing the already active mode still leaves component editing.

**Geometry > Component Selection** and **Tools > Component Selection** provide:

- **Select Touching Edges/Faces**: convert using any incident pick, instead of
  requiring a complete boundary.
- **Grow Component Selection**: add one neighbour step. Vertices connect along
  edges, edges at shared endpoints, and faces across shared boundary edges.
- **Shrink Component Selection**: remove one boundary ring. A wholly selected
  closed brush stays selected because it has no unselected neighbour.
- **Select Connected Components**: follow those neighbours to select the whole
  connected part of each object containing a pick.

Patch points use four neighbours along rows and columns, without diagonals or
wrapping. Shrink also removes the patch's open outer border. These operations
work independently inside each picked brush or patch; they do not jump into
other objects at coincident positions. They preserve object selection and
geometry history and cancel pending edits. Visibility affects which objects
expose handles, but camera occlusion does not prevent topology selection from
reaching a back-facing component. If the handle limit would truncate a
conversion, the command leaves the current mode and picks intact and asks for
fewer selected objects.

The read-only `map select-components` query uses the same rules:

```sh
vibestudio --cli map select-components room.map --set brush:6/face/4 --operation convert --to vertex --json
vibestudio --cli map select-components room.map --set brush:6/vertex/0 --operation convert --to face --coverage touching --json
vibestudio --cli map select-components room.map --set patch:0/vertex/12 --operation grow --steps 2 --json
```

Repeat `--set` for multiple objects. One object must use one source kind;
repeated sets of that kind are merged. `--coverage complete` is the conversion
default. Grow and Shrink accept `--steps 1..256`; `connected` follows the whole
graph. Patch selectors always use `vertex`. JSON includes sorted `sets`,
counts and `selectors` that can be passed as `--set` values to
`map transform-components`. Empty results are valid. The query writes no map
and accepts no `--output`; explicit CLI IDs inspect objects independently of
GUI visibility. Missing/stale IDs fail the whole query. Re-query after any
topology-changing edit.

Closing a map or opening a different one clears its component mode, handles
and picks. Viewport selection commands require an active component mode.

Drag the labelled **X**, **Y** or **Z** control to move along that world axis.
Drag the centre or a selected handle to move in the camera plane. The current
grid snaps the movement distance and preserves spacing between picked handles.
A dashed outline previews the rebuilt brush or patch control grid. **Checking
geometry…** means validation is still running; releasing waits for that result.
Invalid geometry or a locked object leaves the whole selection unchanged.
A valid release creates one map undo step, including across several brushes.

**Esc** cancels a drag, then clears picks, then leaves component mode. A changed
document, selection, camera, grid or tool also cancels a pending drag. Navigation
remains available between edits. **Edit Brush Components…** and **Edit Patch
Control Points…** provide numeric, keyboard-accessible editing. **Remove Selected
Components** and **Delete Selection** use the shared camera/plan picks.

Handle display is bounded to 128 objects and 16,384 handles; a limit message
asks you to select fewer objects. The preview is a wire outline over the original
surface. Numeric and direct rotation, scale and shear controls are described
below. Arbitrary point insertion gestures and full native profile acceptance
remain incomplete. VibeStudio is still pre-alpha.

### Transform picked components

With **Edit Vertices**, **Edit Edges** or **Edit Faces** active, pick handles and
choose **Transform Components…** from **Geometry** or **Tools > Vertices**.
Choose **Move**, **Rotate**, **Scale** or **Shear**, then enter the values.
Rotation angles are degrees; shear is displacement along one axis per unit
along a different axis. Scale accepts negative factors for reflection.
The default pivot is the centre of the picked corners' combined bounds, across
all affected brushes and patches. **Use a custom pivot** supplies exact XYZ.
**Grid** snaps the resulting corner positions; **No snapping** preserves the
numeric transform. Coincident corners merge only with **Allow coincident
vertices to merge**; every brush must remain a three-dimensional convex solid.

**Rotate 90° Left/Right**, **Rotate Selection…**, **Scale Selection…**,
**Resize Selection…**, **Shear…**, mirroring and grid snapping target picked
components while component mode is active. Resize opens component scale
controls; **Rotate Selection…** turns the picks about X, Y and Z at once.
No picked handles means no edit. Surviving handles stay selected after a
successful transform. Brushes and patch control points share one atomic undo
step; a locked object, invalid solid or changed selection cancels the batch.
The progress window supports cancellation without publishing a partial edit.

**Snap Picked Vertex…** in **Geometry** or **Tools > Vertices** moves one
geometric brush vertex to another existing vertex. Select the source, open the
command and choose a target by brush/vertex ID and XYZ. Targets come from the
editable brushes whose component handles are shown. Coincident source vertices
in several brushes can move together; different source positions are refused.
The TrenchBroom profile also offers **Alt+Shift+click** on the target in any
view. Use overlap cycling first when a plan pick includes different depths.
The exact target bypasses grid rounding and permits coincident-corner merging,
while convexity, scene locks and **Lock Component UVs** still apply. A refused
merge leaves the map untouched; a successful snap has one undo step. Choosing
the source itself changes nothing. Patch points are not quick-snap targets.

Component movement and insertion show labelled X/Y/Z distance segments.
The constrained segment is thicker; a dashed trace means geometry is still
being checked. Axis letters and distances remain visible in high-contrast
themes. Numeric component movement accepts displacements up to ±65536 units
so corners can cross the valid world; resulting corners and pivots remain
bounded by ±32768 units.
In plan views the selected object's name returns when the component drag or
insertion ends, keeping the active distance readout clear.

**Lock Component UVs** is separate from object **Texture Lock** and defaults
off; the TrenchBroom profile toggles it with <kbd>U</kbd>. It applies to
ordinary component drags, numeric transforms and **Round Selection to Grid**,
preserving the original texture coordinates at corresponding brush corners.
Patch control point UV values stay attached to their points.

Some deformations cannot keep every corner's coordinates in one planar face
projection: drag a corner so a side becomes a trapezoid, for example. With
**Fit UVs When Lock Fails** on (the default), that face keeps the coordinates
of three of its corners, preferring corners the edit did not move, and the
other corners drift, as NetRadiant Custom's texture vertex lock does. The status
bar says how many faces were fitted. A classic face that cannot show the fitted
mapping takes the nearest classic mapping instead (one rotation and two scales),
unless **Allow map-wide Valve 220 conversion** is on. Turn **Fit UVs When Lock
Fails** off to refuse such edits instead. Both commands are in **Geometry** and
**Tools > Vertices**, and **Transform Components…** offers the fit for one
transform.

**Allow map-wide Valve 220 conversion** permits classic maps to change mapping
syntax when necessary; all classic faces convert together, preserving untouched
geometry and UVs. Undo restores the dialect. Topology insertion/splitting/removal
and face-plane extrusion use their own documented mapping policies.

Choose **Rotate Components**, **Scale Components** or **Shear Components** in
**Geometry** or the Tools tab to manipulate picked components directly in a
plan or camera view. Drag a labelled rotation ring, an XYZ scale handle or
**All** for uniform scale. Shear labels name the changed axis followed by the
axis that determines the offset: **X/Y** changes X in proportion to Y. Rotate
rings support multiple full turns. Scale can pass through zero into a
reflection, but a flat brush cannot be applied. Use **Move Components** to
return to ordinary dragging. Split and extrusion tools switch back to movement.

**Move Component Pivot** provides XYZ and view-plane pivot handles without
editing geometry or adding undo steps. The custom pivot is shared across views
and numeric component transforms. **Reset Component Pivot** restores the picked
corners' combined bounds centre. A custom pivot lasts until reset, leaving
component mode or opening another map.

**Component Manipulator Options…** provides exact pivot coordinates and
persistent rotation, scale and shear steps. Defaults are 15°, 0.125 and 0.125;
**No snapping** disables a step. Scale snaps factors relative to 1. Pivot
movement uses the current map grid. Direct affine gestures use these angle and
factor steps, without snapping final corner coordinates or merging corners;
use **Transform Components…** for final-grid snapping or an explicit merge.

The dashed preview shows checked geometry. **Checking geometry…** keeps a
release pending until the current result arrives. **Esc**, changing picks or
changing tools cancels it. A rejected result leaves the map untouched; a valid
release creates one undo step. **Transform Components…** and the options dialog
provide keyboard-accessible numeric alternatives to dragging.

For automation, inspect component IDs with `map brush-components`. Patch
control-point IDs are zero-based `row * width + column`. Repeat `--set` once per object, with up to
256 objects and 4,096 component IDs per object:

```sh
vibestudio --cli map transform-components room.map --set brush:6/vertex/0,1,2,3,4,5,6,7 --operation rotate --axis z --angle 15 --pivot 0,0,0 --uv-lock on --allow-valve220 --output rotated.map
vibestudio --cli map transform-components room.map --set brush:6/face/4 --set patch:0/vertex/0,1,2 --operation move --delta 0,0,8 --output reshaped.map
vibestudio --cli map transform-components room.map --set brush:6/vertex/0 --operation snap --target brush:6/vertex/1 --output snapped.map
```

`move` requires `--delta x,y,z`; `rotate` requires `--axis x|y|z` and `--angle`;
`scale` requires `--factors x,y,z`; `shear` requires distinct `--axis` and
`--along` axes and a `--factor`. Optional `--grid`, `--allow-collapse`,
`--uv-lock off|on|fit` (default off), `--dry-run` and `--overwrite` share the
editor's validation and save service. `on` refuses a deformation the exact lock
cannot hold; `fit` keeps three corners instead, and the JSON result counts the
`fittedFaces`. `--allow-valve220` requires UV lock on or fit. Component IDs
describe the current topology; inspect again after editing it.

`snap` requires one `--target brush:ID/vertex/ID` and brush-vertex source sets
at one geometric position. It computes the exact displacement and enables
merging automatically. `--grid`, `--pivot` and `--delta` are refused for this
operation. Dry runs report the target, displacement and collapsed-corner count;
UV policy, locked-object checks and protected output paths are shared with the
other component transforms. The CLI may name any existing brush vertex target.

### Transform independently picked faces

**Status: Partial.** Faces picked by **Select Textured** can be moved, rotated,
scaled and sheared without selecting their brushes, as in NetRadiant Custom's
face mode:

1. Choose a material and run **Select Textured**.
2. Turn on **Edit Faces**. The status bar says how many picked faces join the
   edit; no brush needs to be selected.
3. Press the arrow keys in a plan view (one grid step, or eight with
   **Shift**), drag with **Rotate Components**, **Scale Components** or
   **Shear Components**, or choose **Transform Components…**. The dialog states
   that it includes faces picked by **Select Textured**.

Face handles you pick on selected brushes move in the same edit, and the
default pivot is the centre of everything picked. The edit uses the shared
component transform: the picked faces' corners move, each brush is rebuilt as a
convex solid, **Lock Component UVs** applies, and the whole edit is one undo
step. Afterwards each pick stays on the face that carries its moved plane; a
face the rebuild removes is dropped, and the status bar says how many picks
remain. **Undo** and **Redo** restore both the faces and their picks.

The edit is refused, leaving the map unchanged, when a picked brush is hidden,
filtered, locked, inside a closed group or changed since the query, or when the
picks span more than 256 brushes. **Edit Vertices** and **Edit Edges** leave
independent faces out, and splitting, extrusion and vertex insertion use handles
only. Every operation moves the face's corners, as TrenchBroom's face tool does
when it moves a face; NetRadiant Custom rotates and scales the face plane
instead, which can give a different shape when the neighbouring faces are not
perpendicular.

From the command line, pass the returned face targets with `--face`:

```sh
vibestudio --cli map select-textured room.map --material stone --json
vibestudio --cli map transform-components room.map --face face:6:5 --operation rotate --axis z --angle 15 --uv-lock on --allow-valve220 --output turned.map
```

See [Move, rotate and scale textured faces](cli.md#move-rotate-and-scale-textured-faces)
for the options and the JSON fields.

### Add and remove brush corners

Choose **Geometry > Insert Vertex** to add a corner directly in a plan view or
the camera. Point at an edge or face of a selected brush: a diamond with a plus
marks the proposed grid point. Drag it outward until the dashed, checked hull
appears, then release. The point can be anywhere along the edge or face, rather
than only its midpoint. Existing vertex handles keep their selection behaviour.
The TrenchBroom profile also offers **Shift+drag** while **Edit Vertices** is
active. Movement constraints and grid snapping follow your
[component drag profile](editor-profiles.md#move-and-insert-components). With the
TrenchBroom defaults, camera drags stay horizontal, **Alt** moves vertically,
and **Ctrl** uses absolute grid coordinates. Plan views ignore Alt. Press
**Shift** during movement to constrain its dominant axis; the Shift used to
start insertion does not initially constrain it. Other profiles retain
view-plane insertion with an optional Alt Z constraint. Use a front or side
view to raise an edge that is flat in the top view.

Coincident edges or faces of selected brushes are edited together. Each
original corner must survive; an interior, flat, locked or invalid result
refuses the entire edit. **Checking geometry…** means validation is still
running. Releasing waits for the latest result; **Esc**, changing the selection
or losing focus cancels an unfinished drag. Success selects the new
vertices across all panes and creates one undo step. Turn **Insert Vertex** off
or choose **Move Components** to return to ordinary movement. Camera insertion
respects **X-ray Component Handles** and the current visible surfaces.

**Edit Brush Components…** lets you change a brush's topology with a preview.
Enter **New vertex X**, **Y** and **Z**, then choose **Insert Vertex**. The grid
snaps these coordinates. Place the point outside an existing face or edge to
make a new corner; points inside the brush or on its flat surfaces cannot be
stored as additional corners in a brush map. The new corner becomes selected.

Choose vertices, edges or faces, then **Remove Selected Components**. Removing
an edge or face removes all its vertices and rebuilds the solid from those
left. A result that is flat or otherwise invalid is refused. **Undo** restores
the geometry and selection; **Apply** commits the draft as one map edit.
Materials, flags and stored texture parameters are retained, but a deformed
face does not have texture lock.

To split an edge or face in the main views, choose **Edit Edges** or **Edit
Faces**, pick its handles, and turn on **Geometry > Split Edge/Face**. Drag a
handle in a plan view or use the camera's XYZ/centre controls. Each picked
edge midpoint or face centre becomes a new corner at the dragged position.
The dashed outline shows the checked convex result. Release applies it as
one undo step; **Esc** cancels, including while **Checking geometry…** is shown.
The new vertices are selected in every pane so you can continue shaping them.
Turn **Split Edge/Face** off to resume ordinary component movement.

Splitting preserves the original corners. A movement into the brush, a point
that remains flat, a locked object or any invalid result refuses the whole
operation. Use **Edit Brush Components… > Split Selected Edges/Faces** with
the XYZ movement fields for keyboard and numeric editing. The dialog's
**Allow Vertex Collapse** also permits old corners to disappear when needed;
every requested new corner must still survive. **Apply** commits its draft.
For automation, repeat `--brush` in `map insert-vertices` to apply the same
positions to several brushes atomically:

```sh
vibestudio --cli map insert-vertices room.map --brush 0 --brush 1 --position -32,8,16 --output reshaped.map
```

Patches use **Patch Rows and Columns** instead. Deformation texture lock for
these corner edits remains incomplete. Use the dedicated extrusion tool below
to move a brush plane along its normal.

In the plan views or camera, pick handles with **Edit Vertices**, **Edit Edges** or
**Edit Faces**, then use **Geometry > Remove Selected Components** or
**Delete Selection**. Removing an edge or face removes its incident vertices
and rebuilds the convex solid. All affected brushes change as one undo step.
A locked or invalid brush refuses the entire edit. Preparation runs in the
background; **Cancel** leaves geometry, history and component picks unchanged.
Changing the map, selection, tool or profile while preparing also discards the
result. Undo and redo restore the geometry exactly. Removed brush handles are
cleared so their IDs cannot select different corners after rebuilding.

**Delete Selection** follows the active profile. In **NetRadiant Custom**,
**Delete**, **Backspace** and **Z** remove picked brush components. If component
mode has no picks, they delete the selected objects instead. Picked patch points
are kept while picked brush components are removed; patch points alone never
fall back to deleting objects. **TrenchBroom** and the default profile require
picked components while a component tool is active. **Remove Selected
Components** always requires brush component picks, in every profile.

Individual patch control points cannot be removed; use **Patch Rows and
Columns**. In profiles other than NRC, deselect patch points before removing
brush components. Finish active draw, sweep, drag or surface-edit work before
deleting geometry. In TB clip mode, the shared **Delete Selection** command
removes the last clip point without changing map history. Other profiles keep
geometry deletion disabled during clipping; the viewport's existing
**Delete**/**Backspace** clip-point controls remain available.

### Extrude brush faces

Choose **Geometry > Extrude Faces**, then pick face handles. Drag the camera's
**N** axis or the picked handle in a plan view. The face moves along its outward
normal; adjacent planes stay fixed. Positive distances expand the brush and
negative distances contract it. If the normal points directly into the view,
use another view or **Extrude Faces Precisely…**.

**Split Extrusion** makes a separate brush. Outward extrusion keeps the original
brush and adds the new section; inward extrusion partitions the original brush
at the moved plane. The moved face remains selected in every pane. Turn
**Extrude Faces** off to resume ordinary face component movement.

The dashed preview shows the checked result. Release applies it as one undo
step, including the other linked groups; **Esc** cancels a pending drag. Locked or
invalid geometry refuses the whole edit. Extrusion stops before an original
face disappears. Select one face per brush, up to 128 brushes, with parallel
normals. Opposite normals can move together along the reference face's
direction, but cannot split together. The first selected brush in document
order supplies that reference; **N** and the numeric dialog identify it.
Split sections must be at least 0.02
units thick.

Dragging snaps the distance to grid steps or a face vertex to a grid plane,
whichever needs the smaller adjustment. **Extrude Faces Precisely…** accepts an
exact signed distance and a **Split into separate brushes** checkbox; numeric
distances are not snapped. The current **Texture Lock** setting controls the
moved face's projection. Both split caps retain matching world texture
projection, and materials, surface flags, entity ownership and groups survive
the edit. Other component moves and transforms use the separate
[component UV lock](#transform-picked-components).

For automation, inspect face IDs with `map brush-components`, then run:

```text
vibestudio map extrude-faces room.map --face 0:5 --distance 16 --split --output room-extruded.map
```

Repeat `--face brush:face` for more brushes; the first option supplies the
reference face. `--grid` enables the same snapping;
`--no-texture-lock` keeps the original world projection. Standard `--dry-run`
and overwrite protections apply. JSON reports changed/created counts and active
faces. An extruded brush keeps its place in the file, but a split adds a new
brush at the end of its entity, so inspect IDs again after saving and reopening
a split (see [Save your work](#save-your-work)). These tools are available, but full TB/NRC
gesture parity and real-project acceptance remain open; VibeStudio is pre-alpha.

### Sweep selected faces

In **Edit Faces**, pick one or more brush faces and choose **Geometry > Sweep
Faces…** or **Tools > Brush > Sweep Faces…**. Set the destination translation,
rotation axis and angle, and XYZ scale. **Create Sweep** adds convex sections
between the source and destination, selects the new brushes and records one
undo step. The original brushes remain. Each profile keeps its source entity
and group; linked groups update through the normal scene service.

For direct placement, choose **Sweep Tool** in the same menu or tool shelf
(<kbd>Y</kbd> in the TrenchBroom profile). A compact floating palette shares one
destination with all plan and camera views. Drag **Move** to place the cap,
a labelled rotation ring to turn it, or **Scale** to change its uniform size.
Plans show the ring perpendicular to their view; the camera offers all three
world axes. Successive rotations compose around the destination centre.
The numeric translation, rotation and scale controls stay synchronised.

Movement follows the current grid: in a plan it stays in that plan, and in the
perspective camera it starts on the world XY plane. Hold <kbd>Alt</kbd> to move
vertically in the camera. During movement, <kbd>Shift</kbd> constrains the
strongest axis and <kbd>Ctrl</kbd> snaps to absolute world-grid coordinates.
Start the centre drag with no modifiers or with <kbd>Alt</kbd>; rotation and
scale handles require a plain press. Scale follows the rotated source arm,
snapping to the closest grid-plane crossing; it clamps to 0.01 instead of
reflecting the profile. **Decrease Scale** and **Increase Scale** move that arm
by one grid step, or one world unit with snapping off. Rotation uses the angle
step in **Component Manipulator Options**.

Releasing a handle retains the draft. **Create Sweep**, or <kbd>Enter</kbd> while
a level view has focus, applies the latest checked result. In a level view,
<kbd>[</kbd> and <kbd>]</kbd> change the sweep scale while the tool is active.
Custom shortcuts for these three named commands replace their local defaults;
an explicitly unbound command has no local default. <kbd>Escape</kbd> cancels
the current drag, then resets a changed destination, then leaves an unchanged
tool. Closing the palette or toggling **Sweep Tool** cancels the draft directly.
Changing the grid during a drag cancels that gesture.

**Arc** follows a circular bend with any translation along the rotation axis
added as a rise. **Straight** moves the cap centre along a straight line while
rotating and scaling the profile. **S-bend** follows the combined source normal
and its rotated destination direction. The rotation uses the shortest turn:
330° is −30°, and 360° is unchanged. Opposing source normals cancel; S-bend then
uses a straight centre path. Translation and scale use world axes about the
centre of the combined picked-face bounds.

Choose 1–64 **Segments** and 1–8 **Iterations**; defaults are eight segments and
one iteration. Each iteration continues the previous destination transform,
including its rotation and scale. **Round generated stations to whole units**
starts on. The first source station stays exact, even off-grid; turn rounding
off for fractional stations. A collapsed or out-of-world section is omitted
and listed with its brush, face, iteration and segment. If no valid sections
remain, creation is disabled. Inspect these details before applying.

The preview offers **Sweep and source**, **Swept brushes** and **Source objects**,
using available project/package material images. New faces use **Material** and
default mapping/flags; original surfaces stay unchanged. Changing controls
invalidates the old preview immediately. Closing the dialog cancels preparation;
a changed document, package or face selection prevents a stale result applying.
Nothing is saved until the normal map save operation.

The main views show destination outlines and checked section edges. Pending
caps use dashes; **Show textured preview** expands the palette to inspect the
material-aware source/result preview. A changed map, selection, scene context,
package or editor shortcut/profile closes the viewport tool. Source geometry
is retained until explicit creation, and a stale draft cannot overwrite newer
work.

The tool accepts up to 64 distinct faces and 4096 attempted sections, with
coordinates inside ±32768 units and scale components of at least 0.01. This
workflow is **Partial**: textured section previews within the main camera,
dense production maps, complete TB gesture parity and native platform/input
acceptance remain open.

Inspect face IDs with `map brush-components`, then use the same service through:

```text
vibestudio --cli map sweep maps/start.map --set brush:12/face/4 --delta "64,64,32" --axis "0,0,1" --angle 90 --path arc --segments 8 --texture stone/wall --output maps/swept.map --json
```

Repeat **--set brush:ID/face/IDs** for additional profiles. **--scale x,y,z**,
**--iterations**, **--alignment integer|free** and **--path arc|straight|s-bend**
match the dialog. Standard **--dry-run** and overwrite protections apply. JSON
reports attempted/created counts, new brush IDs and every skipped section;
text output also lists omissions. Reinspect IDs after saving and reopening,
because native map source order can renumber them.

### Place entities from the camera

Aim at a brush or patch, select a point class on the **Entities** tab, then choose **Place at Camera Surface**.
A small centre reticle appears when camera placement is ready.
The entity uses the surface at the camera centre, the current grid and the active **Create in** layer.
Its definition bounds stay outside the surface; classes without bounds use an 8-unit marker.
**Clearance** adds a gap. Sloped surfaces can leave a slightly larger gap after grid snapping.
This checks the sampled surface, not collisions against the rest of the map.

Placement is one undo step and uses the same save, recovery and scene-lock checks as plan creation.
Wait for the camera to finish rendering and complete any active drawing or transform before placing.
In Doom and Hexen maps, select a thing type and aim at a sector floor. Things use native whole-unit
coordinates; clearance is ignored and a snapped point outside the visible floor is refused.
New UDMF thing placement remains planned.

### Draw brushes from the camera

Choose **Draw Brush** in the camera tool list. To build from an existing floor or wall, aim the centre crosshair at
it and choose **Use Camera Surface**. The construction grid moves to that point and **Direction** chooses the side
towards the camera. A sloped face supplies the nearest X/Y, X/Z or Y/Z plane; this does not create a slanted work plane.
If the view is still rendering or there is no reachable surface, the readout explains why the previous plane stays in use.

You can also choose a plane and enter **Base**, **Depth** and **Direction** yourself. **Use Work Zone** takes the last
selection's depth range, or 0–64 in an empty map. Drag a footprint, use the wheel to change depth, then release to create
one brush. **Positive (+)** and **Negative (−)** extend from the plane along its hidden axis. **Numeric Brush…** uses
the same plane, direction and material for exact bounds or another primitive shape.

The dashed draft appears in the camera and plan views. <kbd>Esc</kbd> or **Cancel Draft** discards it; release commits
one undo step. Changing the camera, plane, direction, grid, selection, material or tool also cancels a draft. Choosing
**Use Camera Surface** discards any current draft before sampling the new plane.

### Build with shapes

The **Shapes** tab chooses what brush drawing makes, as the object bar does in Hammer and J.A.C.K.: **Box**,
**Wedge**, **Cylinder**, **Cone** and **Sphere** are one brush each, and **Arch**, **Ring**, **Stairs** and **Room** are
several. A shape fills the box you draw, in the map's own face format and the material chosen on the **Textures**
tab, and is one undo step that selects its brushes.

On a Quake III map the tab also offers patch shapes, as Radiant's Patch menu makes them: **Patch Plane**, **Patch
Cylinder**, **Square Patch Cylinder**, **Patch Cone**, **Patch Sphere**, **Patch Bevel** and **Patch End Cap**. Each is
one patch filling the box; **Segments** sets how many spans go round a cylinder, cone or sphere.

1. Choose a shape. Its settings appear below it; the box has none.
2. Choose **Draw in a View**, then drag a box in a 2D view or the 3D view. **Add at View Centre** puts one in the
   middle of the active 2D view instead.
3. To turn existing brushes into the shape, select them and choose **Replace Selected Brushes**: the shape fills their
   bounds, as the Radiant editors' arbitrary-sided brush commands do. Brushes of a brush entity are left alone. To
   rebuild each brush in its own bounds instead, brush entities included, see
   [Rebuild brushes as prisms, cones and spheres](#rebuild-brushes-as-prisms-cones-and-spheres).

| Setting | What it does |
| --- | --- |
| **Axis** | The axis a cylinder or cone runs along, a sphere's poles, a wedge's height, and what an arch or ring turns about. **Across the View** follows the view you draw in, so an arch drawn in the **Front** view stands up as a doorway. |
| **Sides** or **Segments** | How round a cylinder, cone or sphere is, or how many brushes make an arch or ring. A cylinder takes up to 126 sides and a cone up to 127, the most a brush's 128 faces allow; a sphere's sides times **Bands** is at most 128. |
| **Fit** | How a cylinder, cone or sphere sits in the box. **Fill the Box** stretches it to every side of the box. **Round on the Larger Side** builds it as NetRadiant Custom does: round on the box's larger side with a face square to the first axis across the view, so a four-sided cylinder is a square and an eight-sided one keeps the box's own faces; a sphere is a true sphere on the largest side. Such a shape can overhang the box's shorter sides. |
| **Walls** | How thick an arch, ring or room is. Walls as thick as an arch is wide close it into slices. |
| **Sweep** and **Start** | How far round an arch goes, and where it starts, in degrees from the view's right towards its top. |
| **Steps** and **Climbs** | How many solid steps stairs have, and which way they rise: along the longer side, or towards +X, −X, +Y or −Y. |

On a Doom map the tab offers sector shapes instead: **Rectangle** and **Polygon** make one sector, **Stairs** a row
of step sectors each **Step height** above the last, as Doom Builder's stair builder does, and **Grid** the footprint
cut into **Columns** and **Rows**. Choose **Draw in a View** and drag a box in the **Top** view, inside a room or
out in the void, and the shape fills it, as Doom Builder's rectangle and ellipse modes do; keep dragging boxes for
more, and press <kbd>Escape</kbd> to stop. **Add at View Centre** adds one of the **Width** and **Depth** you set in the
middle of the view instead. For a sector of any outline, use **Draw Sector** on the tool bar. Rebuild the nodes
before playing.

### Rebuild brushes as prisms, cones and spheres

**Partial:** NetRadiant Custom's Brush menu shapes, on Quake-family maps. Select brushes, or a brush entity, and choose
**Make Prism…**, **Make Cone…** or **Make Sphere…** (**Edit** > **Brush Shapes**, the **Geometry** menu above the
views, the **Brush Shapes** section of the **Tools** tab, or command search), then give the number of sides. Each brush
is rebuilt in place within its own bounds, round on its larger side with a face square to the first axis, in the
material chosen on the **Textures** tab:

- a prism or cone runs along the active 2D view's depth axis: Z in **Top**, Y in **Front** and X in **Side**. A cone's
  base is at the low end and its point over the middle of the high end.
- a sphere keeps its poles on Z, with as many latitude bands as sides, up to 11 of each.

**Make 3-Sided Prism** to **Make 9-Sided Prism** do the same without asking. In the NetRadiant Custom profile they are
<kbd>Ctrl</kbd>+<kbd>3</kbd> to <kbd>Ctrl</kbd>+<kbd>9</kbd> while a 2D view or the camera has the keyboard and a brush
is selected; anywhere else, or with nothing selected, those keys still switch work modes.

The brushes keep their numbers, their brush entity, layer or group and their place in the file, and the selection stays
as it was. Their faces get a fresh default alignment and keep the brush's content flags. The whole change is one undo
step. Hidden brushes, and brushes too small for that many sides, are left as they were and named in the status bar; a
brush on a locked layer, or outside the open group, stops the change. `map add-shape --in-place --fit circle` does the
same from the [command line](cli.md).

### Create a brush from typed points

**Partial:** TrenchBroom's debug **Create Brush** as an ordinary command, on Quake-family maps. Choose **Create Brush
from Points…** (**Edit** > **Brush Shapes**, the **Create** menu above the views, the **Brush Shapes** section of the
**Tools** tab, or command search) and type four or more points. The new brush is the convex solid that encloses them.

1. Type the points in **Points**, in any of these forms. Each line can use a different one, and text after `//` is
   ignored:
   - `x,y,z` groups separated by spaces or semicolons: `0,0,0 128,0,0 0,128,0 0,0,64`
   - one or more `x y z` points per line
   - TrenchBroom's bracketed form: `(0 0 0) (128 0 0) (0 128 0) (0 0 64)`
2. **Add Selected Corners** appends the corners of the selected brushes, or of the picked faces, edges and vertices,
   in the same form [Copy Vertex Coordinates](#copy-vertex-coordinates) writes.
3. Check the material in **Material**; it starts as the one chosen on the **Textures** tab.
4. Watch the status line and the preview. They say how many corners and faces the brush will have, and how many
   points add no corner because they lie inside the brush or on its faces. Points all in one place, on one line or on
   one plane cannot enclose a volume and are refused, as are more than 4,096 points or a result with more than 128
   faces or 256 corners.
5. Choose **Create Brush**.

The brush goes to worldspawn in the map's own face format, into the open group or the active layer, and is selected.
It is one undo step; a locked destination stops it, and a linked group's copies get it too. The dialog remembers the
last points you typed. `map add-brush --points` does the same from the [command line](cli.md).

### Assemble a brush from clicked points

**Partial:** TrenchBroom's **Brush Tool**, on Quake-family maps. Choose **Assemble Brush Tool** (<kbd>B</kbd> in the
TrenchBroom profile; **Edit** > **Brush Shapes**, the **Create** menu above the views, the **Brush Shapes** section of
the **Tools** tab, or command search). The 2D views and the camera share one set of points, drawn as squares, with the
outline or brush they make. While the tool is on, clicks add points instead of selecting.

In the camera:

- Click a brush face to add a point on the grid in that face's plane. Where no face is under the pointer, the point
  goes on the construction plane that **Draw Brush** uses, the work zone's floor unless you change it.
- Drag from a face to draw a rectangle on its plane, its corners rounded outwards to the grid.
- Double-click a face to add its corners.
- Hold <kbd>Shift</kbd> over a flat outline, which fills to show it can be picked, and drag to extrude it along its
  normal in grid steps.

In a 2D view:

- Click to add a grid point at the depth of the last point, or at the work zone's floor.
- Drag over empty space for a rectangle at that depth. Drag a point to move it, and any points drawn on top of it,
  across the view.
- <kbd>Shift</kbd>-drag a flat outline in a view that shows its side to extrude it: draw a rectangle in **Top**, then
  extrude it upwards in **Front**.

The points are checked in the background as you add them. The readout says how many corners and faces the brush will
have, or why there is no brush yet: fewer than four points, or points on one line or one plane. A dashed line shows
the outline while it is flat and a solid one the checked brush. Press <kbd>Enter</kbd> in any view, or choose **Create
Assembled Brush**, to add it in the material chosen on the **Textures** tab. As with **Create Brush from Points…**, it
goes into the open group or the active layer, is selected and is one undo step, and a locked layer stops it with a
message. The points then clear and the tool stays on.

<kbd>Backspace</kbd> or <kbd>Delete</kbd> removes the last point. <kbd>Esc</kbd> cancels a drag, then clears the points,
then leaves the tool; choosing another tool, such as the clip tool or a vertex mode, also leaves it. Hidden brushes
cannot be clicked. Up to 4,096 points within ±32768 units are kept. `map add-brush --points` with `--extrude dx,dy,dz`
builds the same brushes from the [command line](cli.md). To type or paste exact points, use
[Create Brush from Points…](#create-a-brush-from-typed-points).

### Copy vertex coordinates

**Partial:** TrenchBroom's debug **Print Vertices** as an ordinary command. Choose **Copy Vertex Coordinates**
(**Edit** > **Brush Shapes**, the **Geometry** menu above the views, the **Vertices** section of the **Tools** tab, or
command search). With faces, edges or vertices picked in a component mode, it copies their corners; otherwise it copies
every corner of each selected brush, including the brushes of a selected brush entity. Hidden and filtered brushes are
left out, and the map does not change.

Each brush or component gets a comment line such as `// brush:3` or `// brush:3 face:2 base/metal`, followed by its
corners as `(x y z)` at full precision: the shortest decimal that reads back as exactly the stored value. The same text
goes to the **Activity** panel. Paste it into **Create Brush from Points…**, a script or a report.
`map brush-components` lists one or more brushes, with each face's corner coordinates, from the
[command line](cli.md).

### Work with layers

**Partial:** layers work on Quake, Quake II, Quake III Arena and Doom maps. Create and name them in the **Scene**
section of the **Outliner** tab; these commands use them straight from the views.

| To | Do this |
| --- | --- |
| Select whole layers | Select an object in each layer and choose **Expand Selection to Layers**, as in NetRadiant Custom. Every visible, unlocked object in those layers joins the selection, nested groups included. Closed groups stay whole, and a group with a hidden, locked or filtered member is left out; the status bar says how many. An open group keeps the result inside it. A brush entity joins through its brushes, and worldspawn belongs to no layer. |
| Move objects to a layer | Choose **Move to Layer…** for a menu of the layers at the pointer, or the **Move to Layer** submenu of the right-click menu, as in TrenchBroom, and pick a layer. The selection moves as one undo step. A member of a closed group moves its whole group, and a brush of a brush entity moves the entity with its brushes. Objects moved into a hidden layer leave the selection. The layer the selection is in already, a locked layer, or locked objects grey the entry out with the reason. Close an open group first: layers hold whole groups. |

In the **NetRadiant Custom** profile, <kbd>Shift</kbd>+<kbd>Q</kbd> opens the layers menu and
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Q</kbd> expands the selection: NetRadiant Custom's <kbd>Ctrl</kbd>+<kbd>Q</kbd> stays
**Quit** here. Other profiles leave both without keys; add your own in **Help** > **Keyboard Shortcuts**. Both commands
are in the **Select** menu above the views, on the **Tools** tab and in command search.

Expanding the selection adds an undo step in the TrenchBroom profile only, as **Select Siblings** does. Doom lines and
vertices belong to a layer only when you assign them, so expanding from a sector does not add its unassigned lines.
The command line does the same with `editor scene select --expand-layers` and `editor scene move-to-layer`; see
[Expand and move map layers](cli.md#expand-and-move-map-layers).

### Edit a group

In **Scene**, create a named group and choose **Assign selection**. Clicking any
member then selects the whole closed group, including nested and hidden members.
Transforms move the assembly together. Layers remain separate organisation
containers; they do not select as units.

Choose **Open Group** from **Select**, **Tools > Groups** or command search, or
double-click the group in a viewport. Its contents become editable and other
objects stay visible but cannot be picked or changed. Nested groups remain
closed until opened. The Scene breadcrumb and viewport label show where you are.
Open the containing group before editing individual vertices, edges or faces.

New objects and prefab placements join the open group. **Create in** stays fixed
while it is open. **Close Group** returns to its parent and selects the group;
**Close All Groups** returns to the map and restores the previous creation
destination. Scene has named Open/Close buttons. Double-clicking empty or outside
space also closes the current group when no editing gesture owns that action.

Opening and closing do not dirty the map or add undo steps. Closing cancels
pending component and clip drafts. Locks still apply, and an edit that would
change shared geometry outside the group is refused as a whole. Builds and
packages include the complete map. Group context is not saved between sessions.

**Duplicate**, offset arrays, copy/paste and prefab capture preserve complete
closed groups, nested groups and empty descendants. New groups receive fresh
identities and distinct sibling names. Duplicates stay beside their source;
pasted assemblies and prefabs join **Create in** or the open group. Opening a
group before copying its contents leaves that open ancestor out of the copy.
Partial object copies retain the usual object-copy behaviour. Each insertion,
including its hierarchy, is one undo step.

**Duplicate** on a linked group creates another member of its link set. Copying
one member to the clipboard or a prefab makes an independent group. Copying
two or more members of a set preserves their link with a fresh identity for
each insertion. Groups nested in a linked group are copied with it. Placing a
prefab that contains linked groups turns and moves their frames with it, at any
angle. Group visibility and locks are retained. Clipboard scene metadata that is stale or malformed is
refused before insertion.

Grouped prefabs use `.vprefab` version 2; flat version 1 assets remain supported.
Doom and UDMF thing duplication and arrays also preserve complete groups.
Doom geometry duplication, standalone empty-group copying and automatic removal
of emptied groups remain incomplete.

### Repeat parts with linked groups

**Status: Partial.** Linked groups keep repeated parts of a level the same, as TrenchBroom's do: a row of pillars,
a window frame, a lamp with its light. Groups that share their content are linked groups, and together they form a
link set. None of them is the original: an edit to the content of any one is made to every other, each in its own
place. (Blender's **Duplicate Linked** is the same idea for meshes; Hammer's instances differ, as they draw another
map file.)

1. In the **Scene** section of the **Outliner** tab, type a name and choose **New group**, then select the objects
   and choose **Assign selection**.
2. Select the group (with it closed, clicking any of its objects selects it) and choose **Create Linked Duplicate**
   (the map views' right-click **Groups** menu, **Tools** tab, **Groups**, or command search;
   <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>D</kbd> in the TrenchBroom profile). The duplicate is a new group beside the first, named after it ("Pillar 2"), linked with it and
   selected. **Linked** follows both names in the **Scene** tree.
3. Move, rotate, mirror or scale the duplicate as a whole, at any angle. Only that group changes; it remembers how
   as its frame.
4. Open any of the groups and change it: edit a brush, a key or a texture's alignment, or add, delete or regroup
   objects. Every other group in the set takes the change at once, turned, mirrored or scaled into its own frame,
   and one **Undo** puts them all back. Inside an open group every change is shared, even one that moves all of its
   content.

Groups can sit inside linked groups. A linked duplicate copies them too and links each nested group with its copy,
so an edit inside a nested group reaches every copy of it; linked groups can hold linked groups. A group cannot hold
a group it is linked with.

While the selection holds a linked group, or one is open, the 2D views and the 3D camera draw a box around every group
of its set with its name, and arrows from the selected group to the others. The selected or open group's box is solid
and the rest of its set dashed. In the camera, boxes and arrows are drawn over the scene with a dark edge, and a name
shows where it fits beside its box, clear of the view's readouts and the selection's resize handles.
**Show > Linked Groups** draws every linked group, fainter, in both.

| Command | What it does |
| --- | --- |
| **Create Linked Duplicate** | Duplicates the selected group beside itself and links the two. **Duplicate** on a linked group adds another member to its set. |
| **Select Linked Groups** | Selects every group linked with the selected ones. |
| **Update Linked Groups** | Makes the rest of the set match the selected group, such as groups out of sync. |
| **Separate Linked Groups** | Takes the selected groups out of their set, keeping their content. Several chosen from one set stay linked with each other; a group left on its own is no longer linked. |
| **Extract Linked Groups** | Moves part of an open linked group, and its counterparts, into a new set. |
| **Add Protected Property…**, **Clear Protected Properties** | Keep entity keys local to one group, or share them again. |

A group out of sync no longer matches the rest of its set. That happens when one edit changes two members of a set
in different ways (VibeStudio keeps both edits, where TrenchBroom refuses the edit; values a member protects do
not count), or when an update passes a locked group by. The status bar and **Activity** say so; the **Scene** tree shows **Linked, out of sync**, the 2D
views and the 3D camera draw the group dash-dotted in the warning colour, and saving lists it. Edit one member again, or select the one
to keep and choose **Update Linked Groups**. The **Scene** section's **Linked duplicate**, **Separate**, **Update
linked** and **Select linked** buttons act on the group chosen in its tree.

Linked groups work on Quake-family maps. Links and frames are saved in the map's VibeStudio scene note, a comment at
the end of the file that other editors skip, so they see ordinary brushes and entities; maps saved by earlier
versions, whose frames were quarter turns and a mirror, still open. To keep groups and linked groups in a map you
also edit in TrenchBroom, see [Share layers and groups with TrenchBroom](#share-layers-and-groups-with-trenchbroom).

Corresponding objects retain their identities through edits, **Undo**,
save/reopen and transfers of whole sets through copy/paste and prefabs. Creating or updating linked groups
remains one complete undo step even with a one-step history limit. If a
linked operation cannot fit the scene limits, it leaves the map unchanged.
For older linked maps, use **Update Linked Groups** once to establish
correspondence before using extraction or protected properties.

### Share layers and groups with TrenchBroom

**Status: Partial.** TrenchBroom keeps its layers, groups and linked groups in the map itself, as `func_group`
entities with keys of its own (`_tb_type`, `_tb_id`, `_tb_name`, `_tb_linked_group_id` and `_tb_transformation`), and
an entity lists its protected keys in `_tb_protected_properties`. VibeStudio reads and writes the same keys, so a map
can go back and forth between the two editors with its groups intact.

- **Open** a map TrenchBroom saved: its layers, groups and linked groups appear in the **Scene** tree, each group in its
  layer, the linked groups with their frames and protected keys, so an edit to one reaches the others straight away.
  The `func_group` entities themselves are not listed as entities; their brushes become world brushes in the layer or
  group. A group named the same as a sibling gets a number ("pillar 2"), and TrenchBroom's name is written back.
  Linked groups that no longer match each other open out of sync, and the **Health** tab says which.
- **Save** a map that opened that way and it keeps TrenchBroom's form, so TrenchBroom opens it with the same layers,
  groups and linked groups. To give any map that form, choose **Level map with TrenchBroom groups (*.map)** as the file
  type in **Save As**; later saves keep it, and **Level map (*.map)** goes back to VibeStudio's note alone. A layer's
  colour and **Omit from export**, which VibeStudio does not show, are kept and written back.
- From the CLI, `editor scene save <map> --scene-format trenchbroom --output <file>` writes a map in TrenchBroom's
  form, and `--scene-format vibestudio` takes it out again; any other `editor scene` change accepts the same option.
  See [Command line](cli.md).

A map saved with TrenchBroom groups still carries VibeStudio's scene note, after TrenchBroom's keys. While the note
matches the map, VibeStudio reads the note, because it also holds what TrenchBroom does not keep: hidden and locked
groups, which linked groups are out of sync and which objects correspond. TrenchBroom drops the note when it saves,
and VibeStudio then reads TrenchBroom's keys; a note that no longer matches the map is set aside, and the **Health**
tab says so. A map whose TrenchBroom entities share lines with other text opens with them as ordinary `func_group` entities, as
earlier versions opened it, and the **Health** tab says why.

### Extract part of a set of linked groups

**Status: Partial.** Open a linked group, select some of its contents, then choose
**Extract Linked Groups** in **Tools > Groups** or command search. The selected
contents become a new set of linked groups across every member. Each original
group keeps its remaining contents. Later edits are shared within each set
independently.

Selecting a brush entity extracts all its geometry. Selecting only some of its
brushes or patches splits the owner, retaining that instance's entity properties
and protection on both halves. Each copy keeps its position, supported turn,
mirror, visibility and parent. Geometry and texture coordinates stay in place.
The operation closes the source group, selects its new counterpart, and keeps
the parent editing context open. One **Undo** restores both families, the original
selection and the open group.

A closed group inside the open one is selected whole. In every member it moves
into the new group with everything in it, keeping its link, frame, name and
visibility, so it stays linked with its counterparts. To take part of a nested
group, open that group instead. When the open group is itself inside a linked
group, every group linked with that one takes the same new structure and stays
in sync.

Every instance must be unlocked, contain the selected corresponding objects and
have some contents left behind; nested groups that move must be unlocked too.
Linked groups around the open group must each hold the same counterparts.
Assign each complete brush entity to its group; resolve geometry explicitly
assigned outside that group before extraction. Legacy or incomplete
correspondence requires **Update Linked Groups** first.
Cancellation, stale selection, scene limits and invalid ownership leave the map
unchanged. Preparation runs in the background with cancellable progress.

The shared CLI operation is:

```sh
vibestudio --cli editor scene extract-linked-groups assembly.map --context <group-UUID> --objects brush:6,entity:4 --output extracted.map --json
```

Use `editor scene list` on the current file to inspect node UUIDs and native
selectors; native object numbers can change when saving. To move a nested group,
list all of its objects in `--objects`. The extraction report returns durable
UUIDs for the new groups and counts the nested groups moved (`nestedGroups`).
Add `--dry-run` to validate without writing, or `--overwrite` to replace an
existing output. Extracted sets use the normal copy/paste and prefab workflows
from their containing editing context.
Native cross-platform acceptance remains an open gate.

### Keep entity properties local to one linked group

**Status: Partial.** Open a group and select its entities. The entity inspector's
**Protected** column shows **Local**, **Shared** or **Mixed** with a check box.
Use <kbd>Tab</kbd>/<kbd>Shift</kbd>+<kbd>Tab</kbd> to navigate property cells.
Toggle the check box, or focus its cell and press <kbd>Space</kbd> or <kbd>Enter</kbd>, to
change protection for the selected entities. **Local** preserves that instance's
value when a corresponding linked entity changes. A protected key without a
value remains absent. These keys appear under **Protected keys without values**.

Use **Add Protected Property…** in **Tools > Groups** or command search to set a
key and value together. The corresponding entities of the other linked groups keep
their values or absence. Ordinary value edits still work in the inspector. Property keys follow
the editor's case-insensitive matching. Worldspawn properties cannot be protected.

Unchecking a key restores an existing value from an unprotected corresponding
entity, when available. Otherwise it retains the current value or absence.
**Clear Protected Properties** applies this rule to every protected key on the
selected linked entities. Each operation is one undo step. Clearing ignores
unlinked entities; their individual check boxes remain available. Locks and the
open group's editing boundary apply. An open property dialog refuses to commit
if its map, selection or project has changed.

Protection belongs to each group. A linked update preserves the destination's
value whenever either side protects that key, and retains the destination's own
protection flags. Save/reopen, duplication, clipboard and prefab transfers retain
the flags; separating a linked group retains them too. For older linked maps, use
**Update Linked Groups** before adding protection so correspondence is known.
The scene's authoring metadata stays in the native map's editor trailer; the
compiler and package workflows receive the actual entity values and asset paths.

The same services are available through the CLI:

```text
vibestudio --cli editor scene set-protected-property arena.map --objects entity:2 --key targetname --value local_relay --output local.map
vibestudio --cli editor scene protect-properties local.map --objects entity:2 --keys origin,angle --protected true --output protected.map
vibestudio --cli editor scene clear-protected-properties protected.map --objects entity:2 --output shared.map
```

Use `--protected false` to unprotect selected keys, `--value=` for an explicitly
empty value, `--context <group UUID>` to constrain the operation to an open group,
and `--dry-run` to validate before writing. Replacing a file requires `--overwrite`.
`editor scene list --json` reports each entity's protected keys. Native platform
keyboard/screen-reader acceptance and full TB/NRC parity remain unverified.

## Edit Doom-family maps

| To | Do this |
| --- | --- |
| Add things | Choose **Add Thing…** and give a DoomEd number, or drag a thing type from the **Things** tab onto the **Top** view. |
| Draw a sector | Turn on **Draw Sector** (<kbd>D</kbd>), click the corners, then click the first corner again or press <kbd>Enter</kbd>. <kbd>Backspace</kbd> removes the last corner and <kbd>Esc</kbd> drops the shape. **Add Sector…** takes corners typed as `x,y` pairs. New lines join existing vertices and split the lines they land on. |
| Make sectors from lines | Turn on **Make Sector Mode** (**Tools** tab, **Sectors and Lines**) and click inside any closed shape of lines in the **Top** view, as in Doom Builder's Make Sectors mode: the area becomes a new sector, islands of lines inside it included, and lines that faced nothing there open onto it. Clicking inside an existing sector makes it anew. <kbd>Esc</kbd> turns the mode off. |
| Split or flip lines | **Split Linedefs** splits each selected linedef at its middle; **Flip Linedefs** turns it round, sides and all. |
| Curve lines | **Curve Linedefs…** bends each selected linedef into the number of **Segments** you choose, along an arc whose middle **Bulge**s towards the line's front side (a negative bulge, its back), as Doom Builder's curve mode does. Texture offsets carry on along the pieces. |
| Merge vertices | Select vertices, picking the one to keep last, and choose **Merge Vertices**. |
| Join or merge sectors | **Join Sectors** makes the selected sectors one sector, the one picked last. **Merge Sectors** also removes the lines between them, except lines with a special or tag. |
| Make doors | **Make Door…** turns the selected sectors into doors, as Doom Builder does, with the door, track and ceiling textures you choose. |
| Raise and lower | In a 2D view, <kbd>Page Up</kbd> and <kbd>Page Down</kbd> move floors by 8 units, <kbd>Ctrl</kbd> moves ceilings instead, and <kbd>Shift</kbd> makes the step 1. **Brighten Sectors** and **Darken Sectors** change light by 16. In the camera, as in Doom Builder's visual mode, **Raise Surface at Crosshair** and **Lower Surface at Crosshair** move the floor or ceiling you aim at by 8, and **Brighten Sector at Crosshair** and **Darken Sector at Crosshair** change its light, and **Nudge Texture Left**, **Right**, **Up** and **Down at Crosshair** slide the texture of the wall you aim at by one unit. With **Drag Textures in Camera** on, drag a wall's texture with the left button and it follows the pointer, the camera showing it as you go; letting go makes one undo step, and <kbd>Esc</kbd> puts it back. They are in the **Geometry** menu, the **Tools** tab and command search, and you can give them keys in **Help** > **Keyboard Shortcuts**. |
| Align textures | **Align Textures at Crosshair** lines up the walls joined to the one you aim at in the camera that show the same texture, as Doom Builder's auto-align does: the texture runs on across each join and its rows stay level where floors and ceilings change. In a 2D view, **Align Wall Textures** starts from the first selected linedef and keeps to the selected linedefs when you select several. One undo step. |
| Grade sectors | Select three or more sectors in order and choose **Gradient Floors**, **Gradient Ceilings** or **Gradient Brightness**. |
| Edit fields | The **Inspector** edits a thing's type, angle, position and flags, a sector's heights, flats, light, special and tag, a linedef's special, tag or Hexen arguments, flags and both sides' textures and offsets, and a vertex's position. |
| Delete | <kbd>Delete</kbd> removes the selected things, vertices, linedefs or sectors. |

Edits that move or add lines and vertices leave the map's nodes out of date: rebuild them on the **Build** page before
you play. Raising, lowering and grading keep the nodes current.

For UDMF maps, choose **Edit** > **UDMF Properties…** to change any property of the map or of one object, keeping
comments and unknown fields. Moving, rotating, mirroring, snapping and resizing work too. **Duplicate Selection** and
**Duplicate with Offset…** copy existing things, including their optional height. Copies keep game IDs and action
arguments, so review these in **UDMF Properties…** when a copy needs independent behaviour. Ordinary thing copies keep
nodes current; copying polyobject controls requires a node rebuild. Creating a new thing from a type, deleting things
and creating or copying geometry are not available for UDMF yet.

## Undo, redo and recover

- **Undo** (<kbd>Ctrl</kbd>+<kbd>Z</kbd>) and **Redo** (<kbd>Ctrl</kbd>+<kbd>Y</kbd> or
  <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd>) step through map edits; their buttons sit at the start of the view
  toolbar. The **History** tab lists every edit; press <kbd>Enter</kbd> on a step to go back or forward to it.
- Map undo is separate from package undo on the **Packages** page.
- In the **TrenchBroom** profile, object picks, **Select All**, **Select None**,
  **Invert Selection**, selection from the object/scene lists and opening or
  closing groups also enter this history. Undo restores selection order and the
  group being edited. These steps do not mark the map modified; every history
  position with the saved contents is labelled **saved**. Choosing a different
  selection after Undo starts a new branch and discards Redo. Repeating the same
  selection keeps Redo. NRC and the default profile keep selection and group
  navigation outside history. Face/component picks, visibility and filters do
  not yet have complete TB selection-history behaviour.
- VibeStudio saves a recovery checkpoint of a changed map every minute. **File** > **Recover Maps…** lists the
  checkpoints; **Restore** opens one as unsaved work and never replaces your map file. The window's check box turns
  checkpoints on or off. Checkpoints stay on your computer.

## Copy and paste between editors

In a Quake-family map, <kbd>Ctrl</kbd>+<kbd>C</kbd> copies the selected entities, brushes and patches as plain `.map`
text, the same entity and brush blocks that TrenchBroom and the Radiant editors exchange. <kbd>Ctrl</kbd>+<kbd>V</kbd>
adds `.map` text from the clipboard at its own coordinates and selects it, whichever editor it came from, and
<kbd>Ctrl</kbd>+<kbd>X</kbd> cuts. **Paste with Offset…** previews the pasted objects at an offset you type. Pasted
brushes keep the face format they were copied in, so paste between maps that use the same format. Doom maps have no
clipboard; use **Duplicate Selection** for things.

## Save your work

1. Choose **Save** in the header, or **File** > **Save Map** (<kbd>Ctrl</kbd>+<kbd>S</kbd> while the page has focus).
   VibeStudio writes the map back to its own file, after copying the old file into `.vibestudio/map-backups` beside it.
2. A new map, or one opened from a package, asks for a location first.
3. To write a separate file instead, choose **File** > **Save Map As…** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>S</kbd>).
   VibeStudio suggests the name with `-edited` added and keeps editing the new file.

> [!IMPORTANT]
> **Save** replaces the file you opened, keeping a backup of the old content. Use **Save Map As…** to leave the
> original untouched, for example for a map inside a game installation.

Saving runs in the background with a **Cancel** button; a cancelled or failed save leaves the destination unchanged.
If the file changed outside VibeStudio after you opened it, **Save** refuses: use **Save Map As…**, or **Reload** to
read the outside version. A map is always saved in the format it was opened in. A WAD is written whole, with its other
maps and lumps kept. VibeStudio does not convert between Doom and Quake formats or between face formats, apart from the
optional Valve 220 conversion.

In Quake-family maps, a brush you reshape keeps its place in the file. Component moves, the brush editor, split
extrusion, clipping, CSG and wrap/merge write the changed brush back where the original stood, so the brushes after it
keep their order and the "entity N, brush M" numbers a compiler reports or **Find Brush by Number…** uses. When an edit
makes several pieces, the first piece takes the original's place and the others join the end of the same entity, as
NetRadiant Custom adds a split fragment. New brushes, copies and pastes also go at the end of their entity, so a brush
added to worldspawn still shifts the numbers of brushes in later entities.

## Check maps and entities

- **Health tab.** Lists map problems, entity problems, leaks and compiler checks. With a package open, it also checks
  every texture the map uses. Press <kbd>Enter</kbd> on an entity problem to select that entity.
- **Map tab.** **Worldspawn** edits the map's own keys; **Add Key…** suggests the ones its definition knows.
  **Checklist** shows what a build needs and what is missing, such as a player start, lights, a title, a `wad` key
  for Quake or deathmatch starts for Doom; press <kbd>Enter</kbd> on a row to go to what it is about.
  **View** > **Level Sidebars** > **Show Map Statistics** and **Show Worldspawn Keys** open the tab with that section
  expanded and the keyboard in its list; both are in command search and can be given keys.
- **Entity definitions.** On the **Map** tab, give a Radiant `.def`, Valve `.fgd` or Quake III `.ent` file, or a
  folder of them, under **Entity Definitions** and choose **Load** (**Browse** picks a file). Left empty, VibeStudio
  looks in the project's `.vibestudio/definitions`, `definitions`, `defs`, `scripts`, `base/scripts` and `entities`
  folders. **Project** > **Load Entity Definitions…** does the same. The map is then checked for unknown classes,
  undeclared keys, invalid values, missing required keys, unknown spawnflags, and targets that name nothing. An `.ent`
  file lists placed entities only, so it cannot check key types or spawnflag names.
- **Built-in classes.** With no definitions found, a Quake-family map is checked against the classes of the stock
  game it looks like: Quake, Quake II or Quake III Arena, taken from id Software's GPL game code, with the
  Quake compiler classes VibeMap2 inherits from ericw-tools, such as `func_detail`. A mod's own classes read as unknown until you load its
  definitions, and **Health** and the **Checklist** say which classes were used.
- **Dependencies.** Choose **Dependencies** in the header. The scan reads the package or asset folder open on the
  **Packages** page or, with none open, the project's folders. It lists textures, shader images, models, model
  materials and sounds, marking missing and ambiguous ones; with the game's assets indexed (see
  [Package and release](releases.md#before-you-start)), references the game provides show as **Provided by the
  game**. Tick **Problems only** to filter, **Select in Map** to find the objects that use an asset, and **Copy
  JSON** for the full report. **Export Assets…** writes the resolved files of a package to a new package; when the
  scan read the project, **Package Map…** releases the map instead.
- **Package Map.** Choose **Package Map** in the header to release the map with its build and every custom asset it
  uses, leaving the game's own files out. See [Package and release](releases.md).
- **Leaks.** **Build** > **Load Leak Trail…** draws a compiler `.pts` or `.lin` file over the map.
- **Portals.** **Build** > **Load Portal File…** outlines the vis portals of a compiler `.prt` file (PRT1, PRT1-AM or
  PRT2) in the 2D views and the camera, to see where visibility is cut and where detail or hint brushes would help.
  **Clear Portals** removes them.

## Build and test the map

- On the document bar, pick a compiler profile and choose **Run Profile** to review and run a compile of the open map.
  The compiler reads the saved file, so VibeStudio asks you to save first. **Copy CLI** copies the matching command.
- On the **Build** page, **Use Open Map** builds the map open in Levels and follows it when you open another.
- For Quake, Quake II and Quake III maps, **Build** > **Prepare Build Workspace…** captures the map, unsaved edits
  included, with the open package into a new build folder. **Use in Build** makes it the build input; **Publish
  Prepared Build…** and **Deploy Prepared Build…** package and install the result.

[Build and launch](build-and-launch.md) covers compilers, pipelines, node builders and launching the game.

## Generate a level (optional)

**Generate** in the header plans and builds a sealed level from a short description for Quake, Quake II, Quake III or
Doom. Under **Who plans the rooms**, **The rules, on this machine** needs no AI; **The text model** uses your configured
model, which it names once it is ready, and needs a description to plan from; with it chosen, **AI Settings…** beside
**Preview Request…** opens the AI settings. You review the layout, then open it in Levels as a new, unsaved map or
save it.

**Generator** chooses who builds it:

| Generator | Games and modes | What you get |
| --- | --- | --- |
| **Studio blockout** (Available) | Quake, Quake II, Quake III, Doom; single player, deathmatch, duel | Rooms on a grid of sealed cells, themed textures, liquid pits, monsters |
| **Quake-MapGen** (Partial) | Quake, Quake II, Quake III; single player, deathmatch, duel, capture the flag | Ranked layouts with stairs and raised crossings, a choice of **Map format**, and an **Analysis** of navigation, visibility and lighting |

Quake-MapGen reads the theme from the description, so **Theme**, **Liquid**, **Monsters** and
**Use the project's textures where they fit** are hidden while it is chosen; **Intensity** and **Map format** appear
instead, and **Rooms** goes up to 32 rather than 16. Its findings are advisory: read them under **Details** >
**Analysis**, and keep its full report with **Save Report…**. **Edit with AI** asks your text
model for changes to an open Quake-family map and lists each proposed edit for review before anything changes. AI is
off by default; see [AI assistant](ai.md).

## Work from the keyboard

Every command is also in the menus and in command search. Your [editor profile](editor-profiles.md) can change these
keys, and your own keys in **Help** > **Keyboard Shortcuts** take priority over both. Every Levels command is also
listed, with a short description, in the shared command registry that `vibestudio --cli ui semantics --json` prints,
so scripts and editor profiles name each one by the same id, such as `map.vertex-tool` for **Edit Vertices**.

<details>
<summary>All shortcuts on the Levels page (VibeStudio Default profile)</summary>

| Action | Shortcut |
| --- | --- |
| Open Map / New Map | <kbd>Ctrl</kbd>+<kbd>M</kbd> / <kbd>Ctrl</kbd>+<kbd>N</kbd> |
| Save Map / Save Map As | <kbd>Ctrl</kbd>+<kbd>S</kbd> / <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>S</kbd> |
| Undo / Redo | <kbd>Ctrl</kbd>+<kbd>Z</kbd> / <kbd>Ctrl</kbd>+<kbd>Y</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd> |
| Copy / Cut / Paste | <kbd>Ctrl</kbd>+<kbd>C</kbd> / <kbd>Ctrl</kbd>+<kbd>X</kbd> / <kbd>Ctrl</kbd>+<kbd>V</kbd> |
| Duplicate / Delete | <kbd>Ctrl</kbd>+<kbd>D</kbd> / <kbd>Delete</kbd> |
| Select All / Select None / Invert Selection | <kbd>Ctrl</kbd>+<kbd>A</kbd> / <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>A</kbd> / <kbd>Ctrl</kbd>+<kbd>I</kbd> |
| Hide Selection / Show All Hidden | <kbd>H</kbd> / <kbd>Shift</kbd>+<kbd>H</kbd> |
| Smaller Grid / Larger Grid | <kbd>[</kbd> / <kbd>]</kbd> |
| Clip Tool / Draw Sector / 3D Wireframe | <kbd>X</kbd> / <kbd>D</kbd> / <kbd>W</kbd> |
| Raise / Lower Floor (2D view) | <kbd>Page Up</kbd> / <kbd>Page Down</kbd>; add <kbd>Ctrl</kbd> for ceilings, <kbd>Shift</kbd> for steps of 1 |
| Maximize or Restore Active View (this profile only) | <kbd>Ctrl</kbd>+<kbd>Space</kbd> |
| Frame the selection / the whole map (2D view) | <kbd>F</kbd> / <kbd>Home</kbd> |
| Zoom in / out (2D view) | <kbd>+</kbd> / <kbd>-</kbd> |
| Select the next object / add the next or previous (2D view) | <kbd>Tab</kbd> / <kbd>Ctrl</kbd>+<kbd>Tab</kbd>, <kbd>Shift</kbd>+<kbd>Tab</kbd> |
| Nudge the selection | The profile's **Nudge** keys, one grid step (VibeStudio Default: arrow keys in a 2D view); <kbd>Shift</kbd> moves eight steps where that key is free; see [Nudge the selection](#nudge-the-selection) |
| Pan (2D view) | Arrow keys with nothing selected, or <kbd>Ctrl</kbd>+arrow keys; <kbd>Space</kbd> turns a pan hand on or off |
| Cancel a drag, clear the selection, then leave the view | <kbd>Esc</kbd> |
| Map actions menu | <kbd>Menu</kbd> or <kbd>Shift</kbd>+<kbd>F10</kbd> |

</details>

## Use the command line

The `map` commands use the same services as the page. Commands that change a map write a new file named by
`--output`, preview with `--dry-run`, and need `--overwrite` to replace an existing file. For a WAD, name the map with
`--map-name` (or `--map`). Objects are named like `entity:3`, `brush:12`, `patch:0`, `thing:5`, `vertex:12`,
`linedef:12` and `sector:4`.

```sh
vibestudio --cli map inspect ./maps/start.map --json
vibestudio --cli map move ./maps/start.map --object brush:12 --delta 16,0,0 --output ./maps/start-moved.map --dry-run
```

<details>
<summary>Main map commands</summary>

| Command | What it does |
| --- | --- |
| `map inspect` | Show a map's entities, textures, statistics and validation |
| `map find` | List the objects a query matches, as the **Objects** filter does; `--number 0,12` finds the object a compiler names as `Entity 0, Brush 12` |
| `map new` | Create an empty map or starter room |
| `map edit` | Change entity keys, a face's texture and alignment, or Doom sector, linedef and side fields |
| `map add-entity`, `map add-brush`, `map add-thing` | Add a point entity, a brush shape, the brush enclosing typed points (`--points`) or a Doom thing |
| `map delete`, `map duplicate`, `map paste` | Delete objects, copy them with an offset (`map duplicate --copies N` creates a linear array), or insert `.map` text |
| `map move`, `map rotate`, `map flip`, `map resize`, `map snap` | Transform objects, with texture lock options |
| `map round-to-grid` | Round each brush vertex, patch control point and point-entity origin of `--object` objects onto `--grid`, or only `--set` components, as **Round Selection to Grid** does; optional `--uv-lock off\|on\|fit` |
| `map repeat` | Replay an exported JSON recipe on the current object targets as one checked edit |
| `map add-shape` | Add a shape of the **Shapes** tab, or replace brushes with one |
| `map clip`, `map hollow`, `map carve`, `map intersect`, `map merge-brushes`, `map wrap-merge`, `map snap-vertices` | Brush tools; **Wrap/Merge** fills a convex hull from objects or components; `map hollow --mode touch` or `room` hollows as **Hollow Touching…** or **Make Room** does, with `--caulk`, `--caulk-texture` and `--open-face` for its caulk and openings |
| `map brush-components`, `map move-components`, `map insert-vertices`, `map remove-components`, `map split-components` | Inspect current component IDs, move components, insert corners with repeated `--position x,y,z`, remove incident vertices, or split edges/faces with `--kind edge\|face`, repeated `--component` IDs and `--delta x,y,z`; edits require `--brush` and `--output` |
| `map transform-components` | Move, rotate, scale or shear brush components and patch control points in one batch, or `snap` one geometric brush vertex to `--target brush:ID/vertex/ID`; repeat `--set object:id/kind/component,component`, choose `--operation`, and supply `--output`; shared validation, undo policy and exact UV lock |
| `map select-components` | Read-only conversion, Grow, Shrink or Connected queries with repeated `--set`; conversion requires `--to` and accepts complete/touching coverage; Grow/Shrink accept `--steps 1..256`; JSON selectors feed component transforms |
| `map extrude-faces` | Move parallel brush planes with repeated `--face brush:face`, signed `--distance` and `--output`; optional `--split`, `--grid` and `--no-texture-lock` |
| `map sweep` | Add straight, arc or S-bend sections from repeated `--set brush:ID/face/IDs`; set destination transform, segments, iterations, material and alignment; retain source objects |
| `map create-entity` | Create an entity of a class of the definitions, as the **Create** commands do: a point class at `--origin x,y,z`, or placed against the map from `--camera`, `--plane-point` or `--ray-origin`; a brush class from the `--object` brushes |
| `map tie-entity`, `map move-to-world`, `map detail` | Make brush entities, give their brushes back to the world (`--selected-only` for just the given ones), and make brushes detail or structural (`--structural --policy trenchbroom` also takes them out of brush entities) |
| `map move-to-entity`, `map explode-entity` | Move brushes and patches into an existing entity (`--target last`, `first` or `entity:N`), and give each brush of an entity its own copy of its keys (`--rename-targetnames` for unique names) |
| `map select-region`, `map drop-to-floor` | List the objects a region selection picks, and drop entities to the floor |
| `map align`, `map shear`, `map region` | Line objects up, slant them, and write a region as a sealed map of its own |
| `map replace-key` | Find and replace a key's values across the entities |
| `map entity-colour` | Set the `_color` of entities and of the entities holding brushes (`--colour`), or normalise it (`--normalise`, `--each` for each its own) |
| `map apply-texture`, `map replace-texture`, `map align-textures` | Texture tools; `map replace-texture --face` replaces only picked faces |
| `map add-patch`, `map edit-patch`, `map stitch-patches`, `map cap-patch`, `map patch-matrix`, `map thicken-patch` | Quake III patch tools |
| `map export-prefab`, `map inspect-prefab`, `map insert-prefab` | Save objects as a prefab, inspect one, and place one at `--position` or in place of objects with `--replace` |
| `map connect` | Connect entity owners to the last selected destination; `--profile netradiant-custom` selects NRC naming and two-object rules, `--kill-target` selects KillConnect, and `--game` supplies game context |
| `map draw-sector`, `map draw-stairs`, `map draw-grid`, `map split-linedef`, `map curve-linedefs`, `map flip-linedef`, `map merge-vertices` | Doom geometry tools |
| `map join-sectors`, `map merge-sectors`, `map make-door` | Doom sector tools |
| `map align-walls` | Line up Doom wall textures along the walls joined to one side |
| `map make-sector` | Make a Doom sector of the lines around a point |
| `map shift-sectors`, `map gradient-sectors` | Raise, lower or grade Doom sectors |
| `map inspect-udmf`, `map edit-udmf` | Read and edit UDMF properties |
| `map textures`, `map dependencies` | Check a map's textures and assets against a package or folder; `--installation <id>` marks what the game provides |
| `release plan`, `release publish` | Package and release a map with `--map <map>`; see [Package and release](releases.md) |
| `map render` | Draw an SVG picture of a map |
| `map compile-plan` | Plan a compile with a compiler profile |
| `map recoveries`, `map recover` | List recovery checkpoints and save one to a file |
| `map generate` | Generate a level from a description; `--generator quake-mapgen` builds it with Quake-MapGen, and `--planner ai` asks your text model for the plan |
| `entity definitions`, `entity validate` | List entity classes, and check a map's entities against them |
| `editor scene create-linked-duplicate`, `editor scene select-linked-groups`, `editor scene update-linked-groups`, `editor scene separate-linked-groups` | Duplicate a scene group and link the two, select its link set, make the rest of its set match it, or take groups out of their set (`--id a,b` keeps several linked with each other). `link`, `update-links` and `unlink` still work |
| `editor scene select` | Resolve group selection without writing; use `--objects` and optional `--context <group UUID>`, `--selection` and `--mode replace\|add\|toggle` |
| `editor scene translate` | Move closed groups or objects inside `--context` with `--objects`, `--offset x,y,z` and `--output`; supports `--dry-run` and `--overwrite` |

`vibestudio --cli cli commands` lists every command; see [Command line](cli.md).

</details>

## Place copied objects

**Paste Map Content** (Ctrl+V) follows your profile: TrenchBroom places at the
cursor; other profiles retain the saved coordinates. **Paste at Original
Position**, **Paste at Cursor** and **Paste to Camera** are always available in
**Edit**, search and **Tools > Clipboard**. TB uses Ctrl+Alt+V for original
position; NRC uses Shift+V for camera placement. User key overrides take precedence.

Cursor placement uses the active plan's reference depth or the brush under the
camera pointer. Outside the pane it uses the pane centre. A camera-ray miss uses
a plane 256 units ahead in perspective. In the default profile's orbit view, a
miss stays on the focus plane. **Paste to Camera** centres the assembly using a
snapped translation towards the camera, or the visible focus-plane centre in an
orbit view. Point entities use class bounds, or a 16-unit marker when unknown.
**Paste with Offset…** retains the numeric preview workflow.

To move existing objects to the camera instead, use **Move to Camera** (**Edit** > **Duplicate and Move**, **Tools** >
**Duplicate and Move**, or <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>V</kbd> in the NetRadiant Custom profile). It uses the
**Paste to Camera** rule on the selection: its centre goes to the camera, or the orbit view's focus centre, by a
translation snapped to the grid while **Snap** is on, as one undo step. The objects keep their identities, order and
place in the file, and the clipboard is left alone. In **Edit Vertices**, **Edit Edges** or **Edit Faces** it moves
the picked components. A selection already at the camera stays put. From a script, use `map move --camera`.

These commands preserve ownership, native groups, asset paths and patch UVs,
follow Texture Lock and **Create in**, and add one undo step. The cancellable
worker rejects invalid or stale placement without changing the map. CLI
`map paste` offers camera, plane and ray targets plus `--context`; see
[Clipboard placement](../LEVEL_PASTE.md) for exact snapping, limits and remaining
compatibility gaps. **Status: Partial.**

## Learn more

- [Editor profiles and controls](editor-profiles.md)
- [Level editor design and acceptance notes](../LEVEL_EDITOR.md)
- [Scene layers, groups and locks](../LEVEL_SCENE.md)
- [Placed model appearances](../LEVEL_MODEL_APPEARANCE.md)
- [Map format support](../SUPPORT_MATRIX.md)


### Copy and paste face attributes

In the TrenchBroom profile, **Copy Map Selection** (Ctrl+C) copies picked faces.
Without picks, it copies objects. In any profile, **Copy Surface** copies the
picked component faces in Face mode, or the inspected face in object mode.
Normal **Paste Map Content** (Ctrl+V) recognises face text and applies its last
face to picked faces. In object mode only, it uses all selected brush faces when
no faces are picked. Empty component picks and Vertex/Edge mode never expand a
surface edit to whole brushes. See [component-face targets](#edit-surfaces-from-component-faces).
Geometry and content flags stay in place; one **Undo** restores the batch.
TB face picks cannot be cut.

Face paste transfers material, offsets, rotation, scales and explicit surface
flags/value. Omitted flags preserve existing values. Valve targets keep their
own UV basis and rotate it to the requested angle. **Surfaces > Face attributes**
provides the same mode for the sampled session source. Sampling alone leaves
system clipboard text unchanged. Matrix/non-matrix conversion and patch targets
need another transfer mode. See [Clipboard placement](../LEVEL_PASTE.md#face-clipboard)
for limits, cancellation and CLI equivalents.

### Copy and cut objects

**Copy Map Selection** keeps the owner properties of selected brush-entity
children and copies only those children. **Cut Map Objects** copies whole
objects and removes them as one undo step. Finish component/clipper tools before
cutting. Locks, cancellation, stale selection or clipboard changes preserve the
map; large copies show progress with **Cancel**. Model, sound and texture paths
stay connected to the normal dependency/build/package workflow. See
[object clipboard controls and limits](../LEVEL_PASTE.md#copy-and-cut-objects).


## Select by hierarchy or source line

**Partial:** these commands work on Quake, Quake II and Quake III text maps.
Finish component editing, face picking or clipping before using them.

**Select Siblings** replaces the selection with selectable objects sharing its
parents. A brush inside a brush entity selects that entity's other brushes and
patches. A world brush selects the other objects in its layer, including children
of brush entities. Closed groups stay together; an open group limits selection
to its contents. Selecting objects with several parents includes each parent's
children once. Layers remain separate.

In the **TrenchBroom** profile, **Ctrl+B** selects siblings and **Ctrl+Alt+B** runs
the compiler profile. Other profiles expose the command without a default key.
User shortcut overrides continue to take precedence.

**Select by Source Line Number…** accepts positive, one-based line numbers
separated by commas or spaces. A brush face line selects its brush; a patch line
selects its patch. A brush-entity property or bracket line selects its selectable
children. When a requested line also matches a selectable child, only matching
children are selected. Point-entity lines select that entity. The worldspawn
header and comments between objects select nothing. Valid numbers with no match
clear the selection; invalid input shows an error before running the query.
Localised decimal digits, including Arabic/Indic digits, are accepted.

Line numbers refer to the **source text at last open**, as do object diagnostics.
Edits and saves preserve this reference. New or copied objects acquire source
positions when you save and reopen the map. Use the same map revision as a
compiler diagnostic, or save and reopen before looking up its new line numbers.
Deleting an object removes it from these results; Undo restores its source
identity. Closed groups expand a matching member to the whole group. VibeStudio
scene metadata has no group-bracket line, and foreign TB group/layer metadata
conversion is still incomplete.

Both commands respect hidden, locked and filtered objects and the current open
group. A closed group containing an excluded member is excluded as a whole.
If a grouped entity owns geometry explicitly assigned outside that group, the
group is also excluded; regroup its contents before selecting it as one unit.
Selection changes leave saved map contents untouched. TB records one selection
undo step; NRC and default profiles keep selection outside history. Preparation
is cancellable, and changes to the map, selection, tool or filters discard a
stale result. These commands do not provide Doom WAD/UDMF source-line selection
or face/component selection.

The CLI prints stable selectors for subsequent authoring commands:

```sh
vibestudio --cli map select-siblings maps/start.map --object brush:12 --json
vibestudio --cli map select-lines maps/start.map --line "42, 57" --line 80 --json
vibestudio --cli map select-lines maps/start.map --line 42 --context GROUP-ID --exclude brush:4 --json
```

Repeat **--object**, **--line** or **--exclude** as needed. The two query modes
require their own input option. JSON returns `selection`, `source`,
`sourceSha256`, `context`, `queryOnly: true` and `mapWritten: false`;
source-line queries also return sorted, unique `lines` and
`lineBasis: "loaded-source"`. No file is written and no selection session is
persisted. **--output**, **--overwrite** and **--dry-run** are rejected.

## Edit surfaces from component faces

**Status: Partial.** In **Edit Faces**, the picked brush-face components are the
targets for both **Surfaces > Selection** and **Surfaces > Picked Faces**.
Quick adjustments, **Paste Surface**, plain face-record paste and **Surface
Alignment…** use exactly those faces, including faces of brushes selected
through an entity. Patch control-point picks remain intact and do not become
whole-patch surface targets. The camera highlights the picked brush faces as
you change the selection; leaving Face mode restores whole-object highlights.
Empty face picks and **Edit Vertices/Edges** have no component-derived surface
targets. Independent **Select Textured** faces and query-selected whole patches
remain additional surface targets in every mode; see
[textured surfaces](#select-textured-surfaces). Leave component mode to edit all
whole selected objects.

**Copy Surface** exports the picked component faces as ordered face records and
keeps the last face in the internal surface clipboard. TB's ordinary **Copy**
also uses picked faces; other profiles retain their ordinary object-copy
policy. In object mode, the existing camera/Inspector face targets and whole
brush/patch Selection target still apply. Entering component mode clears old
camera face picks so they cannot silently return when the tool is left.

Surface changes preserve object/component picks and create one content undo
step. Changing component picks or mode cancels queued work and invalidates an
alignment preview. Active geometry gestures block new surface edits. A
selection-inclusive Radiant paste retains the distinction between the hit face
(which receives copied flags) and additional selected faces (which retain their
flags). The shared surface service accepts an explicitly empty captured
selection without expanding it to whole objects.

For automation, **map select-type --mode faces** returns `surfaceTargets` such
as `face:0:2` alongside its zero-based component `selectors`. Each surface target
can be passed directly to **map copy-surface**, **map paste-surface** or material
painting as **--target**. Surface target face numbers are one-based. Queries
remain read-only; authoring commands require an explicit output as usual.

## Step textures by Radiant increments

**Status: Partial.** The **Surfaces** tab's **Adjust** section can step a
texture the way Radiant's Surface Inspector does, on the same target as the
other quick adjustments: the selection, **Picked Faces**, or the picked face
components in Face mode.

- **Rotate about** chooses where **Rotate −** and **Rotate +** turn by the
  **Rotation step**. **Face centre** keeps the middle of each face's texture in
  place. **Texture origin** keeps the offsets, so the texture turns about the
  point where the world origin meets the face's plane (coordinate 0,0 on a
  patch).
- **Scale U −**, **Scale U +**, **Scale V −** and **Scale V +** subtract the
  **Scale step** (0.5 to start with) from each face's texture scale, or add it,
  keeping the texture origin in place. A scale that would reach zero flips its
  sign instead, which mirrors the texture. Brush-primitive faces read their
  scale from the texture matrix, so they need the material's image size from
  the open package. Patches have no scale value: **+** multiplies their texture
  coordinates by the step and **−** divides them by it, as NetRadiant Custom
  does, so a step above 1 works the other way round.

Each command, or a quick burst of them, is one undo step. Hidden objects are
not targets, and a brush in a locked layer refuses the step, with the reason on
the **Surfaces** tab.

The NetRadiant Custom profile starts the **Rotation step** at 45° about the
**Texture origin** and uses that editor's keys: <kbd>Shift</kbd>+<kbd>Page Down</kbd>
for **Rotate Texture −**, <kbd>Shift</kbd>+<kbd>Page Up</kbd> for **Rotate Texture +**,
and <kbd>Ctrl</kbd>+arrow keys for **Texture Scale U −** and **U +** (left and
right) and **V −** and **V +** (down and up). While there is a target these keys
come before the 2D view's <kbd>Ctrl</kbd>+arrow panning; with nothing targeted they
pan as before. Other profiles start at 15° about each **Face centre** and leave
the scale steps without keys. Choosing a profile resets both values; change
them on the tab whenever you like.

Turns follow NetRadiant Custom's direction for each face format. Classic and
Valve 220 faces turn as their stored rotation does; brush-primitive faces always
turn anticlockwise, seen from the front, for **Rotate +**.

On the command line, **map align-textures --scale-step U,V** adds the
increments, and **--degrees N --anchor origin** turns about the texture origin
(**--anchor centre** is the default):

```sh
vibestudio --cli map align-textures room.map --object brush:12 --scale-step 0,0.5 --output scaled.map --json
vibestudio --cli map align-textures room.map --object brush:12 --degrees 45 --anchor origin --output turned.map
```

## Move and flip textures from the camera

**Status: Partial.** These commands move and mirror textures the way the 3D
camera shows them, as TrenchBroom's camera texture keys do, rather than along
each face's own U and V. They are in the **Surfaces** menu under **Move
Textures from Camera**, on the **Tools** tab and in the command palette:

- **Move Textures Up**, **Down**, **Left** and **Right** move the texture one
  grid size, in texels, up, down, left or right on screen.
- The **(Coarse)** forms move twice the grid size; the **(Fine)** forms move
  one texel, whatever the grid.
- **Flip Textures Horizontally** mirrors the texture axis that runs across the
  view, and **Flip Textures Vertically** the one that runs up it, about the
  middle of each face, as **Flip U** and **Flip V** do.

For each face, the texture axis that runs most nearly across the view counts
as horizontal, so a turned or mirrored texture still moves the way you see it,
on classic, Valve 220 and brush-primitive faces and on patches. A patch uses
the direction its texture runs at its middle control point.

The commands act on the faces you picked in the camera or with **Select
Textured**. With no face picked they act on the **Surfaces** tab's target, as
the other quick adjustments do. Each command, or a quick burst of them, is one
undo step. Brush-primitive faces and patches need the material's image size
from the open package to move; flips need none. Hidden objects are not
targets, and a locked layer refuses the edit, with the reason on the
**Surfaces** tab.

The TrenchBroom profile gives them its keys in the camera: the arrow keys
move by the grid, <kbd>Shift</kbd>+arrows coarsely, <kbd>Ctrl</kbd>+arrows
finely, <kbd>Ctrl</kbd>+<kbd>F</kbd> flips horizontally and
<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F</kbd> vertically. These keys move
textures only while a face is picked and you are not editing vertices, edges
or faces; otherwise they still nudge, copy and flip the selection. Other
profiles leave the commands without keys; give them your own in
**Help** > **Keyboard Shortcuts**.

On the command line, **map align-textures --camera-shift RIGHT,UP** and
**--camera-flip horizontal|vertical** do the same for a camera named by
**--camera-yaw** and **--camera-pitch**:

```sh
vibestudio --cli map align-textures room.map --face 12:3 --camera-shift 16,0 --camera-yaw 90 --output moved.map --json
```

## Rotate textures by fixed steps

**Status: Partial.** These commands turn textures by a fixed angle, as
TrenchBroom's texture rotation keys do, whatever the **Surfaces** tab's
**Rotation step** says. They are in the **Surfaces** menu under **Rotate
Textures**, on the **Tools** tab and in the command palette:

- **Rotate Textures Clockwise** and **Rotate Textures Counter-clockwise** turn
  the texture 15°.
- The **(Coarse)** forms turn it a quarter turn, 90°; the **(Fine)** forms
  turn it 1°.

Each texture turns the named way as you see the face from the front, the side
the camera shows, on classic, Valve 220 and brush-primitive faces and on
patches, whether the texture reads the right way round or mirrored. The turn
keeps the texture's offsets, so it pivots about the texture origin rather
than the middle of the face; use **Rotate −** and **Rotate +** with **Face
centre** to keep the middle in place.

The commands act on the faces you picked in the camera or with **Select
Textured**, or with no face picked on the **Surfaces** tab's target. Each
command, or a quick burst of them, is one undo step. Patches need the
material's image size from the open package; faces need none. Hidden objects
are not targets, and a locked layer refuses the turn, with the reason on the
**Surfaces** tab.

The TrenchBroom profile gives them its keys in the camera: <kbd>Page Up</kbd>
turns clockwise and <kbd>Page Down</kbd> counter-clockwise, with
<kbd>Shift</kbd> for the coarse turns and <kbd>Ctrl</kbd> for the fine ones.
Like the texture moves, these keys turn textures only while a face is picked
and you are not editing vertices, edges or faces; otherwise they still nudge
and copy the selection along the depth axis. Other profiles leave the commands
without keys.

On the command line, **map align-textures --clockwise N** turns N degrees
clockwise, or counter-clockwise for a negative N:

```sh
vibestudio --cli map align-textures room.map --object brush:12 --clockwise 15 --output turned.map --json
```

## Reset textures

**Status: Partial.** Two commands reset a texture's mapping in one pass,
whatever format each face is written in. They are on the **Surfaces** tab as
**Reset** and **Radiant Reset**, in the **Surfaces** menu, on the **Tools** tab
and in the command palette:

- **Reset Texture Alignment** gives every face no shift and no rotation at
  the default scale (0.5 in Quake III, 1 otherwise) and keeps its kind, as
  TrenchBroom's reset does: classic faces get their natural numbers, Valve 220
  and brush-primitive faces are laid in their own plane, as **Align to Face**
  lays them, and patches get their natural spread.
- **Radiant Texture Reset** resets as NetRadiant Custom's **Reset Texture**
  does: classic faces get their natural numbers, Valve 220 and
  brush-primitive faces take their plane's own basis, and patches are capped,
  projected along their average normal. On floors and ceilings the plane's
  basis runs a quarter turn from **Align to Face**.

Unlike **Align to Face**, neither refuses a sloped classic face, so a
selection that mixes formats needs one pass. Both act on the faces you picked
in the camera or with **Select Textured**, or with no face picked on the
**Surfaces** tab's target, as one undo step. Brush-primitive faces and
patches need the material's image size from the open package. Hidden objects
are not targets, and a locked layer refuses the reset.

The TrenchBroom profile binds <kbd>Shift</kbd>+<kbd>R</kbd> to **Reset Texture
Alignment** and <kbd>Shift</kbd>+<kbd>Alt</kbd>+<kbd>R</kbd> to **Natural
Texture Mapping**, its reset to world-aligned textures. The NetRadiant Custom
profile binds <kbd>Shift</kbd>+<kbd>N</kbd> to **Radiant Texture Reset**.

### Set texture values across the map

**Reset Textures…**, in the **Surfaces** menu and on the **Tools** tab, sets
absolute values, as bobToolz's window of that name does:

1. In **Apply to**, choose **Selection** (the **Surfaces** tab's target, or
   the picked faces) or **Whole map**: every visible, unlocked surface in the
   open group. What the views hide stays as it is.
2. Tick **Only faces using** and name a texture to change only the surfaces
   that use it. Case and a leading `textures/` do not matter.
3. Tick **Rename to** to give the surfaces another texture.
4. Tick each of **Scale U**, **Scale V**, **Shift U**, **Shift V** and
   **Rotation** you want to set, and give its value. Unticked values stay as
   they are.
5. Choose **Reset**.

Each face keeps its format. Valve 220 faces keep their axes for a new scale
and turn them by the change of rotation. Brush-primitive faces take the values
as Radiant's Surface Inspector reads such a face, which needs the material's
image size. Patches store no scale, shift or rotation, so they take only the
new name. The whole reset is one undo step; a locked target refuses it.

On the command line, **map align-textures** takes **--reset-alignment U,V** and
**--radiant-reset U,V**, and the Reset Textures values as **--set-scale-u**,
**--set-scale-v**, **--set-shift-u**, **--set-shift-v**, **--set-rotation**
and **--rename**, for the **--object** selection or the **--whole-map**,
optionally **--only-texture NAME**:

```sh
vibestudio --cli map align-textures room.map --object brush:12 --radiant-reset 0.5,0.5 --output reset.map --json
vibestudio --cli map align-textures room.map --whole-map --only-texture base_wall/concrete --rename base_wall/metal --set-rotation 0 --output retextured.map
```

## Fit and project textures

**Status: Partial.** These commands fit a texture to a face without
stretching it, and lay one flat projection across several surfaces, as
NetRadiant Custom's Surface Inspector does. They are on the **Surfaces** tab,
in the **Surfaces** menu, on the **Tools** tab and in the command palette.

To fit a texture and keep its proportions:

- **Fit Texture Width** (**Fit Width** on the **Surfaces** tab) fits one
  repeat across each face's width and scales its height by the same amount,
  starting it at the face's edge.
- **Fit Texture Height** (**Fit Height**) does the same down the height.
- **Fit Texture Width Only** and **Fit Texture Height Only** (**Fit U** and
  **Fit V**) fit one axis and leave the other as it is.

For other repeat counts, use **Surface Alignment…**: choose **Fit**, set the
axis to keep to 0 and tick **Keep proportions**.

To project a texture, use the **Project texture** buttons. Each lays the
default texture, with no shift or rotation at the default scale, flat across
the surfaces:

- **Project Texture Axially** (**Axial**) projects each face along the world
  axis it faces most, as **Natural** does, and each patch along the axis
  nearest its facing, rather than spreading the texture along the patch.
- **Project Texture from 2D View** (**2D View**) projects straight from the 2D
  view you used last, so every surface shares one projection, reading the
  right way round from that view.
- **Project Texture from Camera** (**Camera**) projects along the 3D camera's
  view in the same way.

The fits act on the **Surfaces** tab's target. The projections act on the
faces you picked in the camera or with **Select Textured**, or with no face
picked on the **Surfaces** tab's target. Each command, or a quick burst of
them, is one undo step. **Project Texture from Camera** keeps using the
camera's last direction when its pane is hidden. The fits need the material's image size from the open
package, except on brush-primitive faces and patches; the projections need it
on brush-primitive faces and patches. Hidden objects are not targets, and a
locked layer refuses the edit, with the reason on the **Surfaces** tab.

A projection leaves some surfaces as they were, and the **Surfaces** tab says
how many: faces it runs along (the walls, when you project from the **Top**
view), and classic faces whose format cannot hold it. A classic face holds a
projection only where it suits the face's own axis, such as the **Top** view's
on a floor, whereas NetRadiant Custom leaves every classic face alone. When no
targeted surface can take the projection, nothing changes and the tab says
why. Unlike NetRadiant Custom, brush-primitive faces and patches take the same
projection as Valve 220 faces, which reads the right way round, and **Fit
Width** keeps a patch's proportions rather than re-tiling it to one repeat.

NetRadiant Custom has no default keys for these commands, so no profile binds
them; give them your own in **Help** > **Keyboard Shortcuts**.

On the command line, **map align-textures --fit U,V --keep-proportions**,
**--project axial|x|y|z|-x|-y|-z|camera** and **--project-direction X,Y,Z** do
the same:

```sh
vibestudio --cli map align-textures room.map --object brush:12 --fit 1,0 --keep-proportions --package ./assets --output fitted.map
vibestudio --cli map align-textures room.map --object brush:12 --project z --output projected.map --json
```

## Edit texture coordinates in the UV Editor

**Status: Partial.** The **UV Editor** shows one brush face's outline, or one
patch's control grid, laid over its texture, so you can drag the texture into
place by eye, as NetRadiant Custom's TexTool plugin does. It works with
classic, Valve 220 and brush-primitive faces and with Quake III patches.

To edit a face's texture:

1. Pick one face in the camera or in Face mode, or select one brush and choose
   its face in the **Inspector**. For a patch, select just that patch.
2. Choose **UV Editor…** in the **Surfaces** menu, on the **Tools** tab, from
   the right-click menu or in command search.
3. Drag any numbered point. The whole texture follows it, and the point lands
   on the **Snap** grid. When you let go, that point becomes the **anchor**,
   drawn as a ringed cross.
4. Drag another point to turn the texture about the anchor. Hold
   <kbd>Shift</kbd> to turn in **Turn step** increments. Drag the anchor
   itself, or inside the face, to move the texture again.
5. Check the preview beside the view, then choose **Apply**. **Cancel** or
   <kbd>Esc</kbd> leaves the map as it was.

A patch's control points move one at a time: drag a point, or drag across
several to select them, holding <kbd>Ctrl</kbd> to add, and drag one of the
selected points to move them together.

The view's keys work while it has keyboard focus:

| Key | Action |
| --- | --- |
| Arrow keys | Nudge by the snap step (<kbd>Shift</kbd>: eight steps). A face moves as a whole; a patch moves its selected points, or all of them. |
| <kbd>[</kbd> and <kbd>]</kbd> | Turn anticlockwise or clockwise by the **Turn step**, about the anchor or else the centre. <kbd>{</kbd> and <kbd>}</kbd> turn by one degree. |
| <kbd>N</kbd>, <kbd>Shift</kbd>+<kbd>N</kbd> | Step to the next or previous point. |
| <kbd>A</kbd> | Make the current point the anchor, or clear it. |
| <kbd>Ctrl</kbd>+<kbd>A</kbd> | Select every patch point. |
| <kbd>Ctrl</kbd>+<kbd>Z</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd> | Undo or redo inside the editor. |
| <kbd>+</kbd>, <kbd>-</kbd>, <kbd>Delete</kbd>, <kbd>Insert</kbd>, the wheel | Zoom (<kbd>Delete</kbd> in and <kbd>Insert</kbd> out, as in TexTool). |
| <kbd>Ctrl</kbd>+arrow keys, right- or middle-drag | Pan. A right-click without dragging opens a menu. |
| <kbd>Home</kbd> or <kbd>F</kbd> | Frame all the points. |
| <kbd>Enter</kbd> | **Apply**. |

The **U** and **V** cells in the point list move a point to exact texel
coordinates; on a face, the whole texture follows. The buttons beside the view
do the same as the keys.

Things to know:

- **Apply** makes every change you made in the editor one undo step. Hidden
  objects, locked layers and objects outside the open group cannot be edited.
- Coordinates are in texels, with U to the right and V down the image. A move
  changes only the face's offsets. A Valve 220 face keeps its scale values, and
  a brush-primitive face turns exactly.
- A classic face whose texture is scaled differently along U and V cannot hold
  every turn. It takes the nearest classic mapping, keeping the anchor in
  place, and the status says so. Tick **Convert to Valve 220 when a turn needs
  it** to keep the turn exactly.
- Brush-primitive faces and patches count their coordinates in repeats, so
  they need the image's size. Without an open package, **Use explicit texture
  size** is ticked at 64 × 64 texels; change it before you edit. Classic and
  Valve 220 faces show a checker when the image is not found.
- The preview shows the face's brush, or the patch, with the edited texture.
  The camera and plan views update when you choose **Apply**.

No profile binds a key to the **UV Editor**, because neither NetRadiant Custom
nor TrenchBroom does; give it one in **Help** > **Keyboard Shortcuts**.

On the command line, `map edit-uv` lists a face's or patch's points in texels
and moves or turns them through the same service:

```sh
vibestudio --cli map edit-uv base.map --target face:3:1 --json
vibestudio --cli map edit-uv base.map --target face:3:1 --rotate 15 --anchor 1 --output base-turned.map
vibestudio --cli map edit-uv curves.map --target patch:0 --move-point 5 --to 32,16 --texture-size 128,128 --output curves-uv.map
```

## Set face flags

**Status: Partial.** Quake II and Quake III brush faces carry content flags,
surface flags and a surface value after their texture. The **Flags** section
of the **Surfaces** tab shows and changes them, as NetRadiant Custom's Surface
Inspector and TrenchBroom's Face Inspector do.

To change a face's flags:

1. Pick faces in the camera, or select brushes. With no face picked, the
   section follows the **Surfaces** tab's target.
2. Open the **Surfaces** tab and find **Flags**. Each flag has a box named as
   the game names it, such as **Light** or **Player clip**. A ticked box is set
   on every targeted face; a partly ticked box is set on some of them.
3. Tick or clear a box. A partly ticked box ticks on every face. Only that flag
   changes; each face keeps its other flags.
4. To set the surface value, such as a Quake II light's brightness, type it in
   **Value** and press <kbd>Enter</kbd>. **Value** is empty when the faces'
   values differ.

**Clear Flags** (**Clear Face Flags** in the **Surfaces** menu, on the
**Tools** tab and in command search) clears every flag but detail and sets the
value to 0, as NetRadiant Custom's **Unset** does.

Each change is one undo step, and the boxes follow **Undo**. The detail flag is
shown but cannot be changed here, because it belongs to whole brushes: use
**Make Detail** or **Make Structural**. Bits the game leaves unnamed are kept,
and the section says when any are set. Hidden objects are not targets, and a
locked layer or a face outside the open group refuses the change, with the
reason on the **Surfaces** tab. Quake maps have no face flags, so the section
has no boxes there. A Quake II map whose faces write no flag numbers yet is
still recognised by its header or its Quake II classes, and only the faces you
change gain the numbers. Texture folders alone do not identify the game for
flag edits: modern Quake maps use them too. Changed flags are written as
signed numbers, as NetRadiant Custom and TrenchBroom write them, so
a flag in the top bit, such as **N64 scroll flip** or **No drop**, appears as
a negative number that every compiler reads. On Quake III maps the compiler reads only the detail flag
from a face, and shader surfaceparms decide the rest in game; the section
says so.

On the command line, `map face-flags` reads and changes the same flags by name:

```sh
vibestudio --cli map face-flags base.map --face 3:1 --json
vibestudio --cli map face-flags base.map --object brush:3 --set-content playerclip --output base-clip.map
```

## Select textured surfaces

**Status: Partial.** **Select Textured** in **Edit**, the viewport **Select**
menu, **Tools > Select**, the object context menu and command search uses the
current material. The NRC profile assigns **Ctrl+Shift+A**; other profiles keep
their existing bindings. The read-only **map select-textured** CLI uses the
same query service. Matching materials do not select every parent brush.
Existing object and independent face picks are retained; matching faces and
whole patches are added. Matching ignores case but preserves whitespace. No
match leaves the supplied picks unchanged. Manipulation handles and their mode
are separate from these surface picks.

New results respect hidden objects, view exclusions, locks and the current
open group. Open a closed group before querying its individual surfaces. This
is a deliberate studio boundary: NRC's native brush-face walker does not apply
the same visibility filtering. Existing hidden picks can remain in query
results; an authoring workflow must check their editability before applying
changes. Queries return the complete result or fail; they do not truncate
results beyond 16,384 combined face and matching patch targets.

```console
vibestudio --cli map select-textured maps/start.map --material stone --json
vibestudio --cli map select-textured maps/start.map --material stone --object entity:4 --face face:12:1 --json
```

Use the returned `surfaceTargets` with the normal material or surface commands.
The source hash and ordered brush-geometry fingerprints identify the queried
snapshot. Re-query after source or geometry changes. Surface-only changes do
not alter the geometry fingerprint; face reordering, changed planes or changed
ownership do. See [CLI query details](cli.md#query-textured-level-surfaces).

Plan views draw independent faces with dashed outlines; the camera highlights
their triangles. The viewport readout and accessible descriptions show the
independent face count. **Surfaces** shows the complete target counts. Existing
vertex, edge, face and patch-point picks keep their exact identities and mode.
If adding matching patches would exceed the current component handle capacity,
the complete query is refused; leave component mode or reduce the object picks.

In object mode, **Surfaces > Selection** includes the selected objects and
independent faces. In component mode it includes picked brush-face components,
independent faces and the whole patches selected by this query. Vertex and edge
picks alone do not provide surface targets. **Picked Faces**, **Copy Surface**,
surface alignment and face-attribute paste use the exact face identities.
**Apply Texture** uses the same selection targets. Surface-only edits and their
undo preserve independent face picks, and transforming the picked faces keeps
them on the moved faces. Other changes to geometry or ownership, hiding,
locking, leaving the group boundary and reloading retire them. Undoing such a
change does not revive stale face indices.

**Select None**, or **Escape** in a viewport with independent face picks, first
clears surface and component picks while keeping whole objects. The next clear
leaves component mode if it is active; a further clear removes whole objects.
An active viewport gesture keeps its normal cancellation priority. Query-picked
patches remain whole-object picks until deselected. The existing **Select by
Texture** command continues to select whole objects.

Independent face picks do not become handles, but in **Edit Faces** mode they
move, rotate, scale and shear with the face handles; see
[Transform independently picked faces](#transform-independently-picked-faces).
Native input, screen-reader behaviour, cross-platform acceptance and
real-project use still need production review.

## Find, replace and reveal textures

**Status: Partial.** **Find and Replace Textures…** (**Edit**, **Tools** >
**Surfaces**, the **Surfaces** menu above the views, the object right-click
menu and command search) opens a window that stays open while you work, as
NetRadiant Custom's Find / Replace window does.

1. Type or choose the texture in **Find**. Picking a tile on the **Textures**
   tab, or an image on the **Textures** surface, fills the field that last had
   focus: **Find** until you click into **Replace with**. The window says which
   field a pick fills. While it is open, <kbd>Enter</kbd> or a double-click on
   a tile fills the field instead of applying the texture.
2. Choose where to look in **Search in**: **Whole map**, **Selected objects**
   (selected brushes and patches, those of selected entities, and Doom lines
   and sectors) or **Picked faces** (faces picked by **Select Textured**,
   **Find** or in the camera, with the whole patches **Select Textured** picked).
3. Read the line under the fields: it counts what will change as you type.
4. Choose **Find** to pick the matching faces and select the matching patches,
   adding to the current picks as **Select Textured** does; on a Doom map it
   selects the lines and sectors instead. Leave **Replace with** empty and
   <kbd>Enter</kbd> finds. Choose **Replace** to change every matching surface
   in scope as one undo step. The window stays open for the next pair;
   **Close** or <kbd>Escape</kbd> hides it.

Hidden objects, view filters, locked objects and anything outside an open
group keep their textures; the count and the status bar say how many uses were
left. Members of a closed group are replaced with the rest of the map, but
**Find** leaves them out until you open the group, as **Select Textured** does.
Replacing keeps each surface's alignment, and face picks survive it.

**Reveal Material** (**Edit**, **Tools** > **Surfaces**, the **Surfaces** menu
and the object right-click menu) shows the selection's material on the
**Textures** tab: it selects and scrolls to the tile, clears a filter that hides
it and makes it the current material for painting and new brushes, as
TrenchBroom's Reveal in texture browser does. Picked faces count first;
otherwise the selected brushes, patches, entities, Doom lines and sectors.
When they use more than one material the command is unavailable, and its
tooltip and status tip name two of them. Neither upstream editor gives these
commands a key; add your own in **Help** > **Keyboard Shortcuts**.

```console
vibestudio --cli map replace-texture maps/start.map --from base/wall --to base/metal --face face:3:1 --face face:3:4 --output maps/start-metal.map
vibestudio --cli map select-textured maps/start.map --material base/wall --object entity:2 --within-selection --json
```

**map replace-texture** takes `--object` objects or `--face` picks; its JSON
reports `scope`, `targets` and `skipped`. See
[Find and replace textures](cli.md#find-and-replace-level-textures). Native
physical input, screen-reader behaviour and macOS and Linux acceptance still
need review.

## Select all of a type

**Status: Partial.** **Select All of Type** is available for Quake-family text
maps in **Edit**, the viewport **Select** menu, **Tools > Select**, the context
menu and command search. NRC uses **Shift+A**. **Select All of This Class**
remains a separate command with its existing behaviour in every profile.

In object mode, the command replaces the current picks with every matching
entity class supplied by the selection. Selected brushes and patches supply
their owner's class; worldspawn supplies no class. Several selected owners can
supply several classes. Class keys and values match exactly, including case
and whitespace. Matching entities include their selectable brush and patch
children, even when the entity has no drawable origin.

When there is no non-world entity class, the command selects brushes and
patches using the **current material** from the texture browser or material
picker. Selecting a world brush, a world patch or nothing therefore uses this
material fallback. Material matching ignores case but keeps whitespace.
A query with no matches clears the selection.

In **Face** mode, the command replaces component picks with matching faces on
selected brushes, including brush geometry represented by selected entities.
It keeps object picks, clears patch-point picks, and creates no map undo step.
A material with no matching faces clears component picks. **Vertex** and
**Edge** modes leave selection unchanged; the GUI disables the action there.
Select fewer objects if the current component handles are truncated. A brush
with invalid geometry cannot contribute a partial face result.

These face picks feed component geometry tools and the shared
[surface-edit and clipboard targets](#edit-surfaces-from-component-faces).

New results respect hidden objects, filters, locks and the open group. Closed
groups are atomic in object mode; a group with an excluded child is skipped.
An ungrouped entity with an excluded child contributes only permitted children,
without its whole-owner selector. Open a closed group before selecting its
faces. Hidden original picks still supply class names, but do not remain in the
replacement selection unless they are visible results.

Object changes follow the profile's history policy: TB records a non-content
selection step, while NRC and default profiles keep selection outside history.
Unchanged results add no step. Preparation is cancellable; changes to the map,
picks, components, material, filters or history policy discard pending results.
Native map bytes, asset references and compiler/package inputs stay unchanged.

```sh
vibestudio --cli map select-type maps/start.map --object brush:12 --object entity:4 --json
vibestudio --cli map select-type maps/start.map --material stone --json
vibestudio --cli map select-type maps/start.map --mode faces --object entity:2 --material stone --json
```

The CLI accepts repeated **--object**, **--exclude** and an optional **--context**
group. **--material** is required for fallback and face queries; class queries
do not need it. **--mode** defaults to **objects**. The **vertices** and **edges**
modes report an unchanged selection. JSON includes `selection`, `matchedClasses`,
`materialFallback`, `componentSelectionChanged`, `sets`, `selectors`,
`componentCount`, `surfaceTargets`, `sourceSha256`, `queryOnly: true` and `mapWritten: false`.
Face selectors such as `brush:12/face/0,3` use the same form as component
transforms. A false `componentSelectionChanged` means retain existing component
picks. No file or selection session is written; write options and **--line**
are rejected. Empty or missing exact `classname` values are treated as an empty
class, rather than triggering material fallback.

## Select connected entities

**Status: Partial.** In a Quake-family text map, **Select Connected Entities**
adds direct incoming and outgoing `target`/`targetname` connections to the
current object selection. Select an entity, brush or patch; brushes and patches
supply their owner's names. The command is in **Edit**, the viewport **Select**
menu, **Tools > Select**, the context menu and command search. NRC uses
**Ctrl+Shift+E**; TB keeps that key for **Select Complete Tall**.

The original picks keep their order. Incoming matches are added first, then
outgoing matches, without duplicates. Names and keys match exactly, including
case and whitespace. Only the names selected at the start are used: repeat the
command to advance another hop. `killtarget` and other game-specific reference
keys are not part of this NRC command. Entities without an origin or drawable
anchor can match. Worldspawn contributes no names and is never added.

Matching entities include their selectable brush and patch children. New
results respect hidden objects, locks, filters and the open group. Closed groups
stay together; a group with an excluded member is skipped. If one child is
excluded, an ungrouped entity contributes its permitted children without adding
the owner that would implicitly select the excluded child. Existing hidden
picks remain selected and still supply connection names.

The command also works on the object selection while vertex, edge or face mode
is active. Finish clipping or an active drag/stroke first. It uses cancellable
preparation; changes to the map, picks, filters or selection-history policy
discard pending results. TB records one non-content selection undo step, while
NRC and default profiles keep selection outside history. A query that adds
nothing creates no undo step. Saved map contents and compiler/package inputs
remain unchanged. Doom WAD/UDMF connected geometry uses its separate command.

```sh
vibestudio --cli map select-connected maps/start.map --object patch:2 --json
vibestudio --cli map select-connected maps/start.map --object entity:3 --context GROUP-ID --exclude brush:4 --json
```

Repeat **--object** and **--exclude** as needed. The read-only JSON response
contains `selection`, `originalSelection`, `added`, `connectionKeys`,
`transitive: false`, `sourceSha256`, `queryOnly: true` and `mapWritten: false`.
No map or selection session is written. **--line**, **--output**, **--overwrite**
and **--dry-run** are rejected. Separate **Select Sources** and **Select Targets**
currently use the drawing graph and have narrower coverage for patch owners
and entities without anchors; they are not aliases for this combined command.

## Select by brush volume

**Status: Partial.** Use **Select Inside**, **Select Touching** or a **Tall**
command from **Select** above the views, the context menu or command search.
The profile determines both the test and what happens to the source selection:

| Profile | Region test | Source objects |
| --- | --- | --- |
| VibeStudio default and other profiles | Combined selection bounds | Retained; replaces selection when matches exist |
| TrenchBroom | Each selected convex brush independently | Removed by Inside, Touching and Tall; one undo step |
| NetRadiant Custom | Each selected brush's axis-aligned bounds independently | Inside removes all selected objects; Touching retains them and adds matches |

A gap between separate selection brushes is excluded in the TB and NRC profiles.
TB requires only brushes; NRC requires at least one explicitly selected brush.
Open a closed group before using its brushes as volumes. Hidden, filtered, locked
and out-of-context targets are excluded. A closed group is tested as one bounds
unit; a partly hidden or locked group is excluded. Point entities use loaded
class bounds, with a 16-unit box for unknown classes. TB patches use a sampled
quadratic surface and grid-edge intersection; NRC uses their control-grid bounds.

**Select Tall** in TB projects each brush onto the active 2D view. The default and
NRC labels are **Select Complete Tall**. **Select Partial Tall** tests overlap of
the footprint. Both ignore depth; Partial Tall and NRC tall commands are
VibeStudio extensions. TB tall commands require a 2D view. TB shortcuts are
**Ctrl+E** (Inside), **Ctrl+T** (Touching) and **Ctrl+Shift+E** (Tall), subject to
shortcut overrides.

A consuming command removes its source volumes even when nothing matches.
**Undo** restores the objects, scene state and original selection together;
**Redo** restores the selected result. Finish component or clipper tools first.
Large queries run in the shared **Select by Region** progress dialogue; **Cancel
operation** keeps the map and selection unchanged. Changes to the map, selection,
view, profile, filters or entity definitions invalidate a pending result.
Save the edited map before building or packaging it.

The CLI uses the same service. A query never writes the source map:

```sh
vibestudio --cli map select-region maps/start.map --object brush:0 --object brush:4 --policy trenchbroom --mode inside --json
vibestudio --cli map select-region maps/start.map --object brush:0 --policy radiant --mode touching --json
vibestudio --cli map select-region maps/start.map --object brush:0 --policy trenchbroom --mode complete-tall --axis z --output maps/selected.map --dry-run --json
```

Remove **--dry-run** to write the result. Use **--overwrite** to replace an existing
output; the normal document writer checks conflicts and retains a backup.
**--policy bounds** is the default. Modes are **inside**, **touching**,
**complete-tall** and **partial-tall**; **--axis x|y|z** defaults to Z.
**--context** opens a scene group for the operation. **--definitions** loads custom definitions; otherwise the inferred game's built-in
catalogue supplies entity bounds. JSON **objects** lists new matches,
**selection** includes retained NRC Touching sources, **removedObjects** counts
selected sources consumed in the prepared result, and **queryOnly**, **dryRun**
and **mapWritten** distinguish preparation from publication. Selection is session
state; it is reported in JSON and is not stored in the saved .map.

Cross-platform native input, screen readers and dense real-project acceptance
remain unverified. NRC model-derived entity bounds, TB game-specific world-limit
clipping for Tall, and complete foreign group/filter semantics remain integration
gaps. Matching TB's grid-edge patch test does not provide continuous curved-surface
intersection. The default profile's empty query keeps its existing selection.

### Apply a smart tag

**Partial.** Load game or project tags under **View** > **Filters** > **Tag File…**.
The command palette and **Edit** > **Smart Tags** then offer **Make &lt;tag&gt;**, and
**Make non-&lt;tag&gt;** for flag and classname tags. You can assign keys to these
commands in **Keyboard Shortcuts**; there are no default keys to conflict with
your editor profile.

Select brushes or patches, or pick individual faces. A texture tag applies a
matching material; a surfaceparm tag offers only loaded shaders declaring that
parameter. If several names match, choose one. Texture patterns can also name
an asset outside the open package. A classname tag needs whole objects: choose
a matching brush class, and the tag's optional `texture` is applied before the
brushes are tied to it. **Make non-&lt;tag&gt;** returns matching brush entities to
worldspawn; it does not restore an earlier texture.

Flag tags set or clear their named bits while retaining every other bit.
Detail requires whole brushes; patches have no face flags. Texture and
surfaceparm tags have no remove command: apply another material instead.
Hidden, filtered, locked and out-of-context targets are refused. Open a closed
group before editing individual faces inside it. Preparation is cancellable,
and each completed command is one undo step. Changes appear in the same map,
linked groups, compiler inputs and package workflow as other surface edits.
The GUI currently refuses the whole target brush when any face on it is
filtered, including a pick of a different, visible face on that brush.

`map smart-tag` provides the same edit and choice query from the CLI. Native
input and macOS/Linux acceptance testing remain open.
