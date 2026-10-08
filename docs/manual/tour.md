# A tour of the studio

This page shows you around the VibeStudio window: the bar along the top, the
pages on the left rail, the side panels, the status bar, and the keys that move
you between them.

On macOS, press Cmd wherever this page says Ctrl, and Option wherever it says
Alt.

> [!TIP]
> If you cannot find something, press <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd>
> and type what you want to do. Every command is in that list.

## The studio bar

One row of controls runs along the top of the window. From left to right:

- The menus: **File**, **Edit**, **View**, **Project**, **Build**, **Tools**
  and **Help**. On macOS they sit in the menu bar at the top of the screen
  instead.
- **Go Back** and **Go Forward**, the arrow buttons: they return you to the
  page and place you were at before, like a web browser. Press
  <kbd>Alt</kbd>+<kbd>Left</kbd> and <kbd>Alt</kbd>+<kbd>Right</kbd>, or use
  the back and forward buttons on your mouse.
- **Open Project Folder…**, **Open Package…** and **Open Map…**, as icon
  buttons. Hold the pointer over any button to see its name and shortcut.
- **Search commands**, in the centre of the window. It opens the Command
  Palette.
- **Run Build Pipeline** (<kbd>F7</kbd>) and **Launch Game**
  (<kbd>Ctrl</kbd>+<kbd>G</kbd>), the two commands that start real work, close
  the row.

The window title names the current page and the open project, so the taskbar
shows where you are.

Choose **Help** > **Documentation** to open the bundled HTML user manual in
your browser. It works offline in release packages. If no bundled manual is
available, as in development builds, the command opens the
[online manual](https://github.com/themuffinator/VibeStudio/blob/main/docs/manual/index.md).
You can also find **Documentation** in the Command Palette.

## Find commands and files

| To | Press | Then |
| --- | --- | --- |
| Run any command | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd> or <kbd>Ctrl</kbd>+<kbd>K</kbd> | Type part of its name, category or description, and press <kbd>Enter</kbd> |
| Open a file | <kbd>Ctrl</kbd>+<kbd>P</kbd> (**File** > **Go to File…**) | Type part of a file or folder name, and press <kbd>Enter</kbd> to open it on the right page |
| Search the current page | <kbd>Ctrl</kbd>+<kbd>F</kbd> (**Edit** > **Find**) | Type in the page's filter or search field; on the **Code** page, the editor's find bar opens |
| Search the whole project | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd> (**Edit** > **Find In Project Files…**) | Search, and optionally replace, in every text file of the open project |

The Command Palette and Go to File match loosely, so you can skip letters:
`opk` finds **Open Package**, and `bwall` finds `brick_wall.tga`. Commands that
cannot run right now stay in the list, dimmed. Go to File lists recent files,
your project's files and the entries of the open package; **File** >
**Open Recent** also lists recent files. In the code editor's find bar,
<kbd>F3</kbd> and <kbd>Shift</kbd>+<kbd>F3</kbd> step through the matches, and
**Edit** > **Replace…** adds a replace row (<kbd>Ctrl</kbd>+<kbd>H</kbd> on
Windows and Linux).

You can also drop a map, package, project folder, shader script or text file
onto the window to open it where it belongs.

## The left rail

The rail on the left switches between the studio's ten pages.

- It rests as icons. Its labels open while the pointer rests on it or keyboard
  focus is in it, and fold away when you leave.
- The pin at its foot, **Keep navigation open**, keeps the labels showing. In a
  narrow window a pinned rail still folds, to give the page room.
- The current page's icon takes the accent colour, with a short bar beside it.
- **Settings** sits at the foot of the rail, apart from the work pages.

To change how the rail behaves, open **Settings** > **Appearance and Language**
and choose **Navigation**: **Collapse to icons automatically** (the default),
**Always show labels** or **Icons only**.

## The pages

| Page | What it's for | Learn more |
| --- | --- | --- |
| **Workspace** (<kbd>Ctrl</kbd>+<kbd>1</kbd>) | Project health and problems, search, recent projects, game installations and AI proposals, plus tiles that say what each work page holds | [Projects](projects.md) |
| **Levels** (<kbd>Ctrl</kbd>+<kbd>2</kbd>) | Open, edit and save Doom and Quake-family maps | [Level editing](levels.md) |
| **Models** (<kbd>Ctrl</kbd>+<kbd>3</kbd>) | MDL, MD2 and MD3 models: previews, mesh editing, props and assemblies | [Models](models.md) |
| **Textures** (<kbd>Ctrl</kbd>+<kbd>4</kbd>) | Textures, sprites and palettes, and the Texture Editor | [Textures](textures.md) |
| **Audio** (<kbd>Ctrl</kbd>+<kbd>5</kbd>) | Sounds with waveform previews, and the Audio Editor | [Audio](audio.md) |
| **Packages** (<kbd>Ctrl</kbd>+<kbd>6</kbd>) | Browse, extract, stage and rebuild PAK, WAD, ZIP and PK3 packages | [Packages](packages.md) |
| **Code** (<kbd>Ctrl</kbd>+<kbd>7</kbd>) | Scripts, configs, shader scripts and QuakeC, with highlighting and project-wide search | [Code and scripts](code.md) |
| **Materials** (<kbd>Ctrl</kbd>+<kbd>8</kbd>) | Textures, Quake III shaders and Doom 3 materials with a live, animated preview, edited as text or as nodes | [Materials and shaders](materials.md) |
| **Build** (<kbd>Ctrl</kbd>+<kbd>9</kbd>) | Compile pipelines, problems, artifacts, compiler tools and game launching | [Build and launch](build-and-launch.md) |
| **Settings** (<kbd>Ctrl</kbd>+<kbd>0</kbd>) | First-run setup, appearance, language, accessibility, AI and extensions | [First-run setup](first-run.md) |

Each page's header names the page, sums up what it shows (for example the open
map's file name with its entity and brush counts) and holds the page's main
actions. A page with nothing open says what it needs and offers buttons to get
started.

## The panels

Three panels open beside the page, on the side across from the rail. They
start closed, so every page gets the full width.

- **Activity** (the Activity Center) lists every task with its progress, log
  and result: opening packages, saves, builds and setup. Select a task to see
  its details. **Cancel** stops a task that can be stopped (<kbd>Esc</kbd> also
  works while the panel has focus), and **Clear Finished** tidies the list.
  Open it with the **Activity** button in the status bar or **View** >
  **Activity Panel**.
- **Inspector** shows settings, setup progress and project diagnostics, with
  the raw details. Open it with the **Inspector** button in the status bar or
  **View** > **Inspector Panel**.
- **Assistant** sends questions about your work to an AI model, with the
  context you choose. Until you opt in to AI, it explains what to turn on. Open
  it with **Tools** > **Assistant**, **View** > **Assistant Panel**, or the AI
  item in the status bar. See [AI assistant](ai.md).

Drag a panel by its title bar to move it to the other side or the bottom, or
use its buttons to float or close it. Open panels return at their saved width
when space permits; long Activity rows shorten to fit. **View** > **Reset Layout**
restores the default proportions and closes the side panels.

## The status bar

The status bar reports what just happened on its left. On its right:

- The **Activity** and **Inspector** buttons open and close those panels.
- Five items name the state of your work in words as well as colour: the
  project (**No project** until you open one), the package (**No package**),
  the game installation (**No game**), the compilers (**No compilers**, or how
  many were found, such as **Compilers 7/9**), and AI (**AI-free** or
  **AI on**).

Hold the pointer over an item for details, or select it to go where you can act
on it: the project and game items open the **Workspace** page, the package item
opens **Packages**, the compilers item opens the **Build** page's
**Toolchain** tab, and the AI item opens the **Assistant**.

## Move around with the keyboard

| Action | Keys |
| --- | --- |
| Command Palette | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>P</kbd> or <kbd>Ctrl</kbd>+<kbd>K</kbd> |
| Go to File | <kbd>Ctrl</kbd>+<kbd>P</kbd> |
| Find on the current page | <kbd>Ctrl</kbd>+<kbd>F</kbd> |
| Switch to a page | <kbd>Ctrl</kbd>+<kbd>1</kbd> to <kbd>Ctrl</kbd>+<kbd>9</kbd>, and <kbd>Ctrl</kbd>+<kbd>0</kbd> for Settings |
| Go back or forward | <kbd>Alt</kbd>+<kbd>Left</kbd>, <kbd>Alt</kbd>+<kbd>Right</kbd> |
| Preferences (**Appearance and Language**) | <kbd>Ctrl</kbd>+<kbd>,</kbd> |
| Run Build Pipeline | <kbd>F7</kbd> |
| Build and Launch | <kbd>F5</kbd> |
| Next or previous build problem | <kbd>F4</kbd>, <kbd>Shift</kbd>+<kbd>F4</kbd> |
| Read aloud, or stop reading | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>U</kbd> |
| About VibeStudio | <kbd>Shift</kbd>+<kbd>F1</kbd> |

Press <kbd>Tab</kbd> and <kbd>Shift</kbd>+<kbd>Tab</kbd> to move between
controls. Two places use Tab themselves. In the code editor Tab indents; on
Windows and Linux, <kbd>Ctrl</kbd>+<kbd>Tab</kbd> leaves the editor. In the
Levels views Tab selects the next object, and <kbd>Esc</kbd> cancels a drag,
then clears the selection, then leaves the view.

**Help** > **Keyboard Shortcuts** lists every command with its keys and the
page they work on. **Change Keys…** gives a command keys of your own, and
**Reset** puts the default back.

## Pick up where you left off

- **Reopen the last session at start** is on by default (**Settings** >
  **Appearance and Language** > **Startup and Recovery**). VibeStudio reopens
  the package, map and code files that were open, on the page you closed it on,
  and the status bar says what it reopened. Opening a file from the command
  line with `--open <path>` skips this.
- If VibeStudio closed unexpectedly, the next start shows
  **The studio closed unexpectedly last time.** in a bar above the page. The
  files that were open are not reopened by themselves, in case one of them
  caused the crash: choose **Reopen Last Session** to open them, or
  **View Report** to read the crash report. Crash reports stay on your computer
  and are listed in **Help** > **Crash Reports…**.
- Unsaved work is checkpointed in the background. **File** >
  **Recover Maps…**, **Recover Packages…**, **Recover Audio…** and
  **Recover Text Documents…** restore it.
- To save your open project, package, map and code tabs to come back to later,
  choose **File** > **Save Workspace As…**; **File** > **Open Workspace…**
  restores them.

## Learn more

- [UX design](../UX_DESIGN.md): the principles behind the window's layout.
- [Portable workspaces](../WORKSPACES.md): what a `.vibeworkspace` file keeps.
