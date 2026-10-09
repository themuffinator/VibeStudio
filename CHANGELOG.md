# Changelog

All notable changes to VibeStudio are recorded here, newest first. The format
follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions
follow [Semantic Versioning](https://semver.org/spec/v2.0.0.html). Changes
collect under **Unreleased** as they land and become a dated version when a
release is cut; [docs/RELEASING.md](docs/RELEASING.md) explains how.

## [Unreleased]

### Added

- **Studio:** First public preview of the integrated studio: the Workspace, Levels, Models, Textures, Audio, Packages, Code, Shaders, Build and Settings pages, the Activity, Inspector and Assistant panels, and a searchable command palette.
- **Levels:** Open and edit Doom-family maps (Doom, Hexen, UDMF properties) and Quake-family `.map` files (Quake, Quake II, Valve 220, Quake III brushes and patches) in 2D and 3D views, with undo, entity validation and a backup copy every time you save.
- **Levels:** Editor profiles modelled on TrenchBroom, NetRadiant Custom, GtkRadiant 1.6.0, QuArK and other editors, so the Levels page answers to controls you already know.
- **Packages:** Browse, preview, filter, extract, compare and validate PAK, PK3/ZIP, WAD and WAD2/WAD3 packages, and stage changes into a new package.
- **Assets:** Decode idTech textures with their palettes, view and edit models (OBJ, MDL, MD2, MD3), and preview, edit and analyse sounds.
- **Code:** An editor for QuakeC, shader scripts, configs and entity definitions with highlighting, find and replace, Go to Definition and a problems list.
- **Build and launch:** Run VibeMap2, VibeMap3, ZDBSP and ZokumBSP through build pipelines with a Problems list, leak trails, and Build and Launch into a game.
- **AI:** An optional assistant and generators, off by default, for OpenAI, Claude, Gemini and local models, with a preview of every request before it is sent.
- **Command line:** `vibestudio --cli` exposes the studio's services as scriptable commands with JSON output.
- **Accessibility and languages:** High-contrast themes, text scaling, keyboard navigation, screen reader metadata and OS text-to-speech; 47 interface languages are registered, though their translations are still to come.
- **Branding:** A new logo, wordmark and app icon set for Windows, macOS and Linux, with brand guidelines in [docs/BRANDING.md](docs/BRANDING.md).
- **Releases:** A Windows installer and portable ZIP, a macOS disk image, a Linux AppImage, offline HTML documentation and a complete source archive, published with checksums by a new release workflow.
- **Documentation:** A task-focused user manual in [docs/manual](docs/manual/index.md), also shipped as offline HTML documentation in every package.
- **Studio:** Help > Documentation opens the bundled HTML user manual, with an online fallback, from the menu or Command Palette.
- Level editor: Camera Above Plans layout and grouped authoring tools; GtkRadiant 1.4/1.5 and QeRadiant profiles bring the catalogue to 22, with NRC/TB aliases; camera surface construction with signed extrusion; undoable duplicate arrays in the GUI and CLI.
- Searchable editor profile browser with default controls and adaptation previews; camera-beside-plans workspace with independent splitter persistence, bookmarks and CLI selection.
- **Levels:** Duplicate UDMF things and create arrays with fractional XYZ offsets, preserving unknown properties, comments, scene groups and exact undo through the GUI and CLI.
- DoomEdit and BSP editor profiles with distinct camera navigation, shortcuts, aliases and documented adaptations; 24 profiles are now selectable.
- **Levels:** Place Create-palette point entities at the camera surface with definition bounds, outward grid snapping and clearance; Doom and Hexen things use visible sector floors, with shared scene locks, undo and save.
- **Levels:** Two tabbed sidebars in the manner of Blender's hold the Outliner, Shapes, Entities, Textures, Models, Sounds, Prefabs, Inspector, Tools (every editing command, grouped), Surfaces, Map, View, Health and History pages; each folds to its tabs, tabs move between sides, and every editor profile family arranges and names them as its editor does.
- **Levels:** A Shapes tab draws or adds boxes, wedges, cylinders, cones and spheres, and arches, rings, stairs and rooms of several brushes, fitted to the box you drag; it can also replace the selected brushes with a shape filling their bounds, and offers rectangle and polygon sectors on Doom maps. `map add-shape` does the same from the command line.
- **Levels:** Tie to Entity, Move to World, Select Inside, Touching, Complete Tall and Partial Tall, Make Detail and Make Structural, CSG Intersect and Drop to Floor, each one undo step with a matching `map` command.
- **Levels:** Models and Sounds tabs preview and place the open package's models and sounds, a Map tab edits worldspawn and shows a pre-build checklist, the View tab filters whole kinds of object with counts, and the Inspector shows and sets the selection's centre and size.
- **Levels:** Built-in Quake, Quake II and Quake III Arena entity classes, drawn from id Software's GPL sources, check and place entities when a project has no definitions of its own.
- **Levels:** Hammer's Align (left, right, top, bottom and centres, as the active view shows them) and Radiant's regions: the View tab keeps the views to a box and saves it as a sealed map with a player start for a quick test compile. `map align` and `map region` do the same from the command line.
- **Levels:** Build > Load Portal File outlines a compiler's vis portals (PRT1, PRT1-AM or PRT2) in the 2D views and the camera, as TrenchBroom's and Radiant's portal viewers do.
- **Levels:** Shear… slants the selection about its centre, as TrenchBroom's shear tool does, and Curve Linedefs… bends Doom linedefs into arcs, as Doom Builder's curve mode does; `map shear` and `map curve-linedefs` do the same from the command line.
- **Levels:** On Doom maps the Shapes tab draws stairs (each step's floor above the last, as Doom Builder's stair builder makes) and grids of sectors; `map draw-stairs` and `map draw-grid` do the same from the command line.
- **Levels:** Replace Key Values… finds a value of one key across the map's entities, or the selected ones, and replaces it, whole or wherever it appears, as one undo step; `map replace-key` does the same from the command line.
- **Levels:** Compile Region writes the region beside the map as a sealed map, unsaved edits included, and compiles it with the chosen profile, as Radiant compiles a region.
- **Levels:** On Doom maps, Raise and Lower Surface at Crosshair and Brighten and Darken Sector at Crosshair change the floor, ceiling or light you aim at in the camera, and Nudge Texture Left, Right, Up and Down at Crosshair slide the texture of the wall you aim at, as Doom Builder's visual mode does.
- **Models:** Open models from every idTech generation: Hexen II and Half-Life MDL, Heretic II FM, MDC, MDS, MDM/MDX, MDR, Ghoul 2 GLM/GLA, IQM, Doom 3 MD5 meshes and animations (with .def animation names), LightWave LWO, ASE and GZDoom KVX voxels, with their companion files. Skeletal models are posed into frames for preview and editing, keep their skeleton when saved, and export as MD5 or IQM; ASE export is added too. `model formats` lists them all. The decoders are tested on synthetic files only so far.
- **Models:** The Mesh Editor takes the Levels page's layout: an outliner and a property sidebar that fold to their tabs, one view or four, and controls profiles modelled on Blender, 3ds Max and MilkShape 3D, each with its own keys, mouse navigation, transform style, panes and sidebar names. Customise, export or import any profile's controls in the editor or with `model profiles` and `model controls`.
- **Materials:** A Materials page (formerly Shaders) for the textures, shaders and materials of idTech 1 to 4: Doom walls, flats, animations and switches, Quake WAD textures, Quake II WALs, Quake III shaders and Doom 3 materials, drawn live and animated by each engine's rules, checked the way the game loads them, and edited as text or as a node graph, with saving into the open package.
- **Command line:** `material list`, `inspect`, `validate`, `render`, `graph`, `edit`, `templates`, `new`, `doom-tables` and `wal` share the Materials page's services.
- **Levels:** Linked copies of scene groups, as TrenchBroom's linked groups: **Create Linked Copy** copies a group beside itself, and an edit inside one copy is then made to every copy in its own place and turn, in the same undo step, while moving, turning a quarter turn or mirroring a whole copy changes only it. **Update Linked Copies**, **Select Linked Copies** and **Separate Linked Copy** complete the set, in the **Scene** section, the **Tools** tab and `editor scene link`, `update-links` and `unlink`.
- **Levels:** **Align Textures at Crosshair** and **Align Wall Textures** line up Doom wall textures along the walls joined to the one you aim at or select, as Doom Builder's auto-align does: textures run on across each join and their rows stay level as floors and ceilings change, in one undo step; `map align-walls` does the same from the command line.
- **Levels:** On Doom maps the **Shapes** tab's **Rectangle**, **Polygon**, **Stairs** and **Grid** are drawn by dragging a box in the **Top** view after **Draw in a View**, as Doom Builder's rectangle and ellipse modes do, inside rooms too.
- **Levels:** The **Shear Tool** slants the selection by dragging the handle in the middle of one of its sides in any 2D view, as TrenchBroom's shear tool does, the opposite side staying put.
- **Levels:** **Make Sector Mode** makes a Doom sector of the existing lines around a click in the **Top** view, islands inside included, as Doom Builder's Make Sectors mode does; `map make-sector` does the same from the command line.
- **Levels:** **Drag Textures in Camera** slides a Doom wall's texture with the mouse in the 3D camera, as in Doom Builder's visual mode, the camera following as you drag, one undo step when you let go.
- **Releases:** Package and Release turns a project, map, model or texture folders into a release with only your own files: a one-time index of each game's own packages leaves out what the game already has, and every custom texture, shader, model, sound and script the work uses is gathered, with the map's build and companion files. Choose **Package Map** on the Levels page, or **Package and Release…** on the Workspace page.
- **Releases:** Release notes and a readme are generated from the project's changelog (Keep a Changelog), with **Record Change** to add entries and suggestions drawn from what changed since the last release; publishing writes the package, readme, notes and a distribution archive, records the release and moves the changes under the new version. The `release` and `install register` commands do the same from the command line.
- **Rendering:** Choose OpenGL or Vulkan for every 3D view and material preview in **Settings** > **Appearance and Language** > **3D Rendering** (**Automatic** by default), see what each renderer found and check both with a test image; `render backends`, `render test`, `render set` and `--renderer` do the same from the command line.

### Changed

- **Releases:** Versions follow Semantic Versioning with pre-release labels that say how mature a build is; the first public preview is `0.1.0-alpha.1`.
- **Documentation:** The README is now a short landing page, and the design records sit behind a documentation index in [docs/README.md](docs/README.md).
- **Levels:** The **Layout** section of the **View** tab offers the view layouts as tiles picturing their panes, and the **Shapes** and **Layout** tiles take as many columns as their labels allow, so they stay whole at 200% text and in longer languages.
- **Levels:** The Surfaces tab is in Target, Adjust and Copy and Paste sections like the other sidebar pages; placing a class from the Entities tab selects the new entity, as placing a model or sound does; Give to Selection on the Models and Sounds tabs is offered as soon as an entity is selected; and their Place sections name the class each makes in the open map, warning when the loaded definitions do not declare it.
- **Levels:** **Dependencies** works without an open package by reading the project's folders, and marks what the game provides once its assets are indexed; `map dependencies --installation` does the same from the command line.
- **Projects:** Project manifests (schema 2) record the project's game and release settings and keep settings they do not know; the Workspace page gains a **Releases** card and flags a linked game whose assets are not indexed.
- **Build:** VibeStudio now compiles Quake and Quake II maps with VibeMap2 and Quake III maps with VibeMap3, its own compilers derived from ericw-tools and from q3map2 in NetRadiant Custom. Compiler ids follow the new names (`ericw-qbsp` is now `vibemap2-bsp`, `q3map2-light` is now `vibemap3-light`, and so on); an old id is refused with a message naming its replacement. Stock ericw-tools and q3map2 builds still work through **Locate…** or `compiler set-path`.
- **Rendering:** The Levels camera, model views, the modeller, the Doom preview and material previews and swatches now draw on the GPU instead of the processor, so large scenes and animated materials stay fluid; a view with no working renderer says why instead of drawing.

### Removed

- **Rendering:** The software 3D renderers (the model rasteriser and the CPU material renderer). 3D views need an OpenGL 3.3 or Vulkan 1.0 driver; Mesa's software drivers still work where there is no GPU.

### Fixed

- **Accessibility and languages:** In right-to-left languages the Activity, Inspector and Assistant panels open on the left, across the page from the navigation rail, and keep the side of the page you gave them when you switch language.
- **Accessibility and languages:** In right-to-left languages, the navigation rail's pin and page icons now mirror their left-to-right places, folded or open, instead of sitting up to 4 pixels nearer the window's edge.
- **Levels:** Map-edit status hints now describe Save Map updating the opened file after a change check and backup.
- **Studio:** Panels restored open retain their saved widths instead of growing to a temporary start-up minimum and squeezing the page.
- **Translations:** Speed up the extraction dry run by reusing catalogue source entries in temporary files, keeping every language checked without changing saved translations.
- Studio: Keep Run Build Pipeline and Launch Game glyph spacing consistent in right-to-left layouts, including live direction changes, without changing left-to-right rendering.
- Level editor grid, snap and selection framing remain available in camera-only and maximised camera views.
- Translation extraction preserves existing translations, plural variants and translator notes when Qt cannot merge a translated locale; offscreen focus checks build and run with Qt 6.10.
- Level Create and Inspector actions wrap translated captions without clipping at enlarged text scales.
- **Build:** The compiled-artifact summary now reads a PRT1-AM portal file's counts in ericw-tools' order (clusters, portals, leaves), so it no longer reports the leaf count as the portal count.
- **Levels:** The Outliner no longer shows "Origin unknown" for the world and brush entities, and counts a single key as "1 key".

[Unreleased]: https://github.com/themuffinator/VibeStudio/commits/main
