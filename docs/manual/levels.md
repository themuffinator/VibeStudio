# Level editing

The **Levels** page opens, edits, checks and saves maps for Doom-family games (Doom and Hexen maps in WADs, including
UDMF) and Quake-family games (Quake, Quake II and Quake III `.map` files), then hands them to the **Build** page.

> [!NOTE]
> **Status: Partial.** Opening, editing, undo, validation and saving work for every format below and are covered by
> automated tests, but none of it has been proven on real production maps. UDMF maps get property and transform editing
> only, and some tools from established editors are still missing; the
> [coverage list](../LEVEL_EDITOR.md#coverage-of-other-editors) says which.

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
| Header | **Dependencies**, **Save**, **New Map**, **Generate**, **Edit with AI**, **Open Map** |
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
  **Transform**, **Align**, **Select**, **Entities**, **Surfaces**, **Patches**, **Sectors and Lines** and **Region and
  Build**. Each button is the command itself, so it greys out, stays pressed and takes keys just as in the menus; a
  group the map's format cannot use is hidden.
- Right-click a tab to move it to the other sidebar, fold the sidebar, show captions under the tab icons, or
  **Reset Sidebars** to where your editor profile keeps them.
- The **View** menu has **Level Sidebars** commands to show each tab and to fold either sidebar. Like every
  command, they are in command search and can be given keys in **Help** > **Keyboard Shortcuts**.
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
- **Grid.** Choose **Grid 1** to **Grid 256** in the grid list, or press <kbd>[</kbd> and <kbd>]</kbd>. With **Snap**
  ticked, drags and arrow-key nudges move in whole grid steps.
- **3D view.** Choose **3D** to show the camera. The VibeStudio Default profile orbits an orthographic view of the map;
  most other profiles fly a first-person camera. Tick **Textures** above the views to draw package images: open the
  game's package or asset folder on the **Packages** page first. **View** > **3D Wireframe** (<kbd>W</kbd>) shows edges
  only.
- **Layout.** The **Layout** menu shows one 2D view, one 3D view, the camera beside a 2D view, four views,
  **Camera Above Plans** (a wide camera above top, front and side), or **Camera Beside Plans**
  (a tall camera beside three stacked plans). Drag the dividers to size the panes;
  each layout remembers its own proportions. Grid, snap and selection framing remain available in a maximised camera.
  **Follow Editor Profile** returns to your profile's arrangement. **Maximize Active View** enlarges the focused pane
  until you restore it, **Equalize View Sizes** evens out the panes, and the link options keep the 2D views centred
  together or following the camera. **Saved Views** stores named camera and plan positions for this map. The
  **Layout** section of the **View** tab offers the same choices as tiles, each picturing its panes with the camera
  pane filled; point at a tile for its full name.
- **Show.** The **Show** menu chooses what the 2D views draw: **Things** (Doom) or **Entities** (Quake), **Sector Fill**
  (Doom), **Grid**, **Vertices**, **Labels** and **Target Links** (Quake). The **View** tab has the same options with
  the grid, snapping, texture locks, layouts and view links.
- **Filters.** The **Filters** section of the **View** tab hides whole kinds of object in every view, each with a
  count: world brushes, brush entities, detail, clip, hint and skip, caulk, sky, liquids, point entities, lights,
  triggers, monsters, items, player starts, paths and models, or on a Doom map each category of thing. **Show Every
  Kind** brings them all back. Filters are separate from hidden objects, so **Show All Hidden** leaves them on, and
  they only change what you see.
- **Region.** To work on one area of a big map, choose **Set to Selection** or **Set to View** in the **Region**
  section of the **View** tab (or **Select** > **Set Region to Selection**). The views shade or hide what lies
  outside the box, as Radiant's regions and Hammer's cordon do; **Clear Region** brings the whole map back.
  **Compile Region** writes the region beside the map as `<name>-region.map`, unsaved edits included, and compiles
  it with the chosen profile, so `<name>-region` can be loaded in the game to test that area quickly. The region
  map keeps what touches the box, sealed by six brushes just outside it, and gets a player start (where the camera
  stands, or in the middle) if it has none. **Save Region As…** writes the same map anywhere you choose. **Run
  Profile** still builds the whole map.
- **Hide and show.** <kbd>H</kbd> hides the selection, <kbd>Shift</kbd>+<kbd>H</kbd> shows everything again, and
  **View** > **Isolate Selection** hides everything else. Hidden objects stay in the map, its builds and its packages.
  To keep visibility with the map, use the check boxes on the **Scene** tab, which can also lock layers and groups.
- **Export Image** writes an SVG picture of the map for reviews and documentation.

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
- The **Edit** menu adds **Select All of This Class**, **Select by Texture…**, **Select Targets**, **Select Sources**
  and, for Doom maps, **Select Connected Geometry**.
- **Select by Region**, in the right-click menu and the **Select** menu above the views, uses the selection as a
  region, as the Radiant editors do: **Select Inside** keeps what lies wholly inside it, **Select Touching** what
  touches it, and **Select Complete Tall** and **Select Partial Tall** do the same seen from the active 2D view,
  whatever the depth. Hidden and filtered objects are left out.
- The **Transform** section at the top of the **Inspector** tab shows where the selection's centre is and how big it
  is. Type a new centre to move it there, or a new size to resize it, keeping its lower corner; point entities
  move but have no size to change.
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
| Place models and sounds | The **Models** and **Sounds** tabs list the open package's models and sounds, with a preview of the chosen model and a waveform you can play. Drag one onto a view, or choose **Place in View**, for a `misc_model` or a speaker entity; **Give to Selection** sets the selected entities' `model` or `noise` key instead. The **Place** section names the class it makes in the open map, and says so when your definitions do not declare it: Quake itself has no model or speaker entity, so a Quake mod must provide them. |
| Duplicate or delete | <kbd>Ctrl</kbd>+<kbd>D</kbd> copies the selection one grid step over; **Duplicate with Offset…** previews an exact offset and a **Copies** count for a linear array. <kbd>Delete</kbd> removes the selection; an entity takes its brushes with it. |
| Move | Drag the selection, nudge it with the arrow keys (<kbd>Shift</kbd> moves eight grid steps; Radiant-style profiles nudge with <kbd>Alt</kbd>+arrow keys), or use **Move Selection…**. |
| Rotate or flip | **Rotate 90° Left**, **Rotate 90° Right**, **Flip Horizontal** and **Flip Vertical** work as the active view shows the map. **Rotate Selection…** previews any angle about the selection centre, the world origin or your own pivot. |
| Align | **Transform** > **Align Left**, **Align Right**, **Align Top**, **Align Bottom**, **Centre Horizontally** and **Centre Vertically** line the selected objects up on the edge or centre of the whole selection, as the active 2D view shows it, as Hammer does. An entity moves with its brushes. |
| Shear | Turn on **Transform** > **Shear Tool** and drag the handle in the middle of a side of the selection in any 2D view: that side slides along itself and the opposite side stays put, slanting the selection, as TrenchBroom's shear tool does. <kbd>Escape</kbd> turns the tool off. **Transform** > **Shear…** slants the selection about its centre by an angle you type. Texture lock follows **Texture Lock**; where the map's face format cannot slant textures, the tool shears without it and says so. |
| Resize | Drag the handles in a 2D view, or the labelled X, Y and Z handles in the 3D view. **Resize Selection…** takes exact sizes and the point that stays in place. |
| Cut with a plane | Turn on the **Clip Tool** (<kbd>X</kbd>), drag a line across the brushes in a 2D view, press <kbd>Tab</kbd> to choose which side stays (or both), then <kbd>Enter</kbd>. **Clip Selection…** cuts along an exact x, y or z plane. |
| Hollow | **Hollow…** turns each selected brush into walls of the thickness you choose, one per face. |
| Carve (CSG subtract) | **Carve** cuts the selected brushes out of every brush they overlap. The carving brushes stay selected, ready to delete; hidden brushes are left alone. |
| Intersect (CSG) | **Intersect** replaces the selected brushes with the one brush where they all overlap, as TrenchBroom does; each face keeps the texture of the face it came from. |
| Make detail | **Make Detail** stops brushes sealing the map or splitting its visibility: in Quake II and Quake III maps it sets each face's detail flag, and in Quake maps it moves the brushes into a `func_detail` for ericw-tools. **Make Structural** undoes it. |
| Drop to the floor | **Drop to Floor** moves the selected point entities straight down onto the brush or patch below, keeping each class's height above it. |
| Merge brushes | **Merge Brushes…** previews their convex union and lets you choose where conflicting materials, mappings and flags come from. |
| Apply a texture | Select brushes or patches and choose **Apply Texture…**, or press <kbd>Enter</kbd> on a tile on the **Textures** tab. **Replace Texture…** swaps one texture for another across the map or the selection. |
| Align textures | Edit a classic or Valve 220 face's shift, rotation and scale on the **Inspector** tab, or use the **Surfaces** tab: **Target** chooses the selection or the inspected face, **Adjust** shifts, rotates, scales, fits or centres it in steps, and **Copy and Paste** carries one face's material, mapping and flags to others. **Surface Alignment…** gives a detailed preview. |
| Paint materials | Choose a material in the **Material** box above the views, set the camera tool to **Paint**, then click or drag across surfaces in the 3D view. Each stroke is one undo step. **Sample** picks up a surface's material instead. |
| Reshape a brush | **Edit Brush Components…** moves its vertices, edges or faces and rebuilds the brush. |
| Make curves (Quake III) | **Add Patch…** creates a plane, cylinder or cone. **Edit Patch Control Points…**, **Stitch Patches…** and **Cap Patch…** reshape, join and close patches. |
| Link entities | Select the entities, picking the target last, and choose **Connect Entities**. The others target it, and it gets a `targetname` if it has none. |
| Replace key values | **Edit** > **Replace Key Values…**, or the search button on the **Inspector**'s **Properties** section, finds a value of one key across the map's entities (or the selected ones) and replaces it: the whole value, or with **Match the whole value** off, every occurrence, such as a name's prefix. It counts the matches as you type, and the change is one undo step. |
| Reuse a group of objects | **Export Selection as Prefab…** or **Save Selection…** on the **Prefabs** tab saves a `.vprefab` file. **Insert Prefab…** places one; the **Prefabs** tab lists those in the project and the open package. |

For repeated steps, columns or props, set **Copies** in **Duplicate with Offset…** to 1–256 additional copies.
With an X offset of 64, the first copy moves 64 units from the source, the second 128, and the third 192.
The preview shows every copy; **Place** selects them all in one undo step. Copies preserve their owners, textures and
scene memberships. The same workflow copies Doom/Hexen/UDMF things. UDMF copies retain fractional XYZ positions,
comments and unknown thing properties; geometry duplication is unavailable. Entity target
names retain ordinary duplicate behaviour: use **Insert Prefab…** when each linked assembly needs independent target
names. An invalid copy or cancellation leaves the entire map unchanged.

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

1. Choose a shape. Its settings appear below it; the box has none.
2. Choose **Draw in a View**, then drag a box in a 2D view or the 3D view. **Add at View Centre** puts one in the
   middle of the active 2D view instead.
3. To turn existing brushes into the shape, select them and choose **Replace Selected Brushes**: the shape fills their
   bounds, as the Radiant editors' arbitrary-sided brush commands do. Brushes of a brush entity are left alone.

| Setting | What it does |
| --- | --- |
| **Axis** | The axis a cylinder or cone runs along, a sphere's poles, a wedge's height, and what an arch or ring turns about. **Across the View** follows the view you draw in, so an arch drawn in the **Front** view stands up as a doorway. |
| **Sides** or **Segments** | How round a cylinder, cone or sphere is, or how many brushes make an arch or ring. |
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

### Repeat parts with linked copies

Linked copies keep repeated parts of a level the same, as TrenchBroom's linked groups do: a row of pillars, a
window frame, a lamp with its light. An edit inside one copy is made to every copy, each in its own place.

1. In the **Scene** section of the **Outliner** tab, type a name and choose **New group**, then select the objects
   and choose **Assign selection**.
2. Select something in the group and choose **Create Linked Copy** (**Tools** tab, **Linked Groups**). The copy is
   a new group beside the first, named after it ("Pillar 2"), with **Linked** after its name in the **Scene** tree.
   Move it where you want it: moving a whole copy moves only that copy.
3. Edit either copy: change a brush, a key, a texture's alignment, add or delete objects in the group. The other
   copies take on the change at once, and one **Undo** puts every copy back.

Each copy keeps its own turn: rotate a whole copy by 90 degrees about the vertical, or flip it, and only that copy
turns, while later edits reach it turned the same way. Turns by other angles turn every copy. A locked copy is left
as it is until it is unlocked. If one edit changes copies in different ways, they are left as they are;
select the copy the others should match and choose **Update Linked Copies**. **Separate Linked Copy** makes a copy
independent again, and **Select Linked Copies** selects every copy. Linked copies work on Quake-family maps.

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

## Check maps and entities

- **Health tab.** Lists map problems, entity problems, leaks and compiler checks. With a package open, it also checks
  every texture the map uses. Press <kbd>Enter</kbd> on an entity problem to select that entity.
- **Map tab.** **Worldspawn** edits the map's own keys; **Add Key…** suggests the ones its definition knows.
  **Checklist** shows what a build needs and what is missing, such as a player start, lights, a title, a `wad` key
  for Quake or deathmatch starts for Doom; press <kbd>Enter</kbd> on a row to go to what it is about.
- **Entity definitions.** On the **Map** tab, give a Radiant `.def`, Valve `.fgd` or Quake III `.ent` file, or a
  folder of them, under **Entity Definitions** and choose **Load** (**Browse** picks a file). Left empty, VibeStudio
  looks in the project's `.vibestudio/definitions`, `definitions`, `defs`, `scripts`, `base/scripts` and `entities`
  folders. **Project** > **Load Entity Definitions…** does the same. The map is then checked for unknown classes,
  undeclared keys, invalid values, missing required keys, unknown spawnflags, and targets that name nothing. An `.ent`
  file lists placed entities only, so it cannot check key types or spawnflag names.
- **Built-in classes.** With no definitions found, a Quake-family map is checked against the classes of the stock
  game it looks like: Quake, Quake II or Quake III Arena, taken from id Software's GPL game code, with Quake's
  ericw-tools compiler classes such as `func_detail`. A mod's own classes read as unknown until you load its
  definitions, and **Health** and the **Checklist** say which classes were used.
- **Dependencies.** Open the map's package or asset folder on the **Packages** page, then choose **Dependencies** in
  the header. The scan lists textures, shader images, models, model materials and sounds, marking missing and
  ambiguous ones. Tick **Problems only** to filter, **Select in Map** to find the objects that use an asset,
  **Copy JSON** for the full report, and **Export Assets…** to write the resolved files to a new package.
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
model. You review the layout, then open it in Levels as a new, unsaved map or save it. **Edit with AI** asks your text
model for changes to an open Quake-family map and lists each proposed edit for review before anything changes. AI is
off by default; see [AI assistant](ai.md).

## Work from the keyboard

Every command is also in the menus and in command search. Your [editor profile](editor-profiles.md) can change these
keys, and your own keys in **Help** > **Keyboard Shortcuts** take priority over both.

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
| Nudge the selection (2D view) | Arrow keys, one grid step; <kbd>Shift</kbd>+arrow keys, eight steps |
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
| `map find` | List the objects a query matches, as the **Objects** filter does |
| `map new` | Create an empty map or starter room |
| `map edit` | Change entity keys, a face's texture and alignment, or Doom sector, linedef and side fields |
| `map add-entity`, `map add-brush`, `map add-thing` | Add a point entity, a brush shape or a Doom thing |
| `map delete`, `map duplicate`, `map paste` | Delete objects, copy them with an offset (`map duplicate --copies N` creates a linear array), or insert `.map` text |
| `map move`, `map rotate`, `map flip`, `map resize`, `map snap` | Transform objects, with texture lock options |
| `map add-shape` | Add a shape of the **Shapes** tab, or replace brushes with one |
| `map clip`, `map hollow`, `map carve`, `map intersect`, `map merge-brushes` | Brush tools |
| `map tie-entity`, `map move-to-world`, `map detail` | Make brush entities, give their brushes back to the world, and make brushes detail |
| `map select-region`, `map drop-to-floor` | List the objects a region selection picks, and drop entities to the floor |
| `map align`, `map shear`, `map region` | Line objects up, slant them, and write a region as a sealed map of its own |
| `map replace-key` | Find and replace a key's values across the entities |
| `map apply-texture`, `map replace-texture`, `map align-textures` | Texture tools |
| `map add-patch`, `map edit-patch`, `map stitch-patches`, `map cap-patch` | Quake III patch tools |
| `map connect` | Make entities target the last one given |
| `map draw-sector`, `map draw-stairs`, `map draw-grid`, `map split-linedef`, `map curve-linedefs`, `map flip-linedef`, `map merge-vertices` | Doom geometry tools |
| `map join-sectors`, `map merge-sectors`, `map make-door` | Doom sector tools |
| `map align-walls` | Line up Doom wall textures along the walls joined to one side |
| `map make-sector` | Make a Doom sector of the lines around a point |
| `map shift-sectors`, `map gradient-sectors` | Raise, lower or grade Doom sectors |
| `map inspect-udmf`, `map edit-udmf` | Read and edit UDMF properties |
| `map textures`, `map dependencies` | Check a map's textures and assets against a package or folder |
| `map render` | Draw an SVG picture of a map |
| `map compile-plan` | Plan a compile with a compiler profile |
| `map recoveries`, `map recover` | List recovery checkpoints and save one to a file |
| `map generate` | Generate a level from a description |
| `entity definitions`, `entity validate` | List entity classes, and check a map's entities against them |
| `editor scene link`, `editor scene update-links`, `editor scene unlink` | Make a linked copy of a scene group, make its copies match it, or separate it |

`vibestudio --cli cli commands` lists every command; see [Command line](cli.md).

</details>

## Learn more

- [Editor profiles and controls](editor-profiles.md)
- [Level editor design and acceptance notes](../LEVEL_EDITOR.md)
- [Scene layers, groups and locks](../LEVEL_SCENE.md)
- [Placed model appearances](../LEVEL_MODEL_APPEARANCE.md)
- [Map format support](../SUPPORT_MATRIX.md)
