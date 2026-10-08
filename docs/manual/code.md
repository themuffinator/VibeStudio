# Code, scripts and shaders

The **Code** page edits the text files in your project, such as QuakeC,
configs, shader scripts and entity definitions, with highlighting, search,
navigation and careful saves. The **Materials** page reads Quake III shader
scripts and checks the textures they use.

> [!NOTE]
> **Status: Partial.** The editor, project search and built-in navigation work
> and have automated tests, but have not been proven in real projects. Live
> diagnostics and semantic completion need a language server you install
> yourself, and C and C++ files have no built-in highlighting. The Shaders page
> reads and checks scripts but cannot edit them graphically yet.

## Open files

Choose **Code** on the rail, or press <kbd>Ctrl</kbd>+<kbd>7</kbd>
(<kbd>Cmd</kbd> on macOS). With a project open:

- The **Files** tab lists the project's source and text files. The list fills
  in the background; **Refresh Tree** scans again after you add files outside
  VibeStudio.
- The **Outline** tab lists the functions, shaders or entity classes in the
  open file; select one to jump to it.
- **Go to File** (<kbd>Ctrl</kbd>+<kbd>P</kbd>) opens any project file from
  anywhere in the studio.
- **New Text File** (<kbd>Ctrl</kbd>+<kbd>N</kbd> on this page) starts an
  untitled tab.

Each file opens in its own tab, and a dot marks unsaved changes.
<kbd>Ctrl</kbd>+<kbd>Page Down</kbd> and <kbd>Ctrl</kbd>+<kbd>Page Up</kbd>
move between tabs, and <kbd>Ctrl</kbd>+<kbd>F4</kbd> closes one. The bar above
the editor shows the file's folders; click a folder to open another file from
it. Scripts opened from inside a package are read-only copies: extract them, or
use **Save File As…**, to edit them.

## Supported files

| Language | Files | Highlighting |
| --- | --- | --- |
| QuakeC | `.qc`, `.qh`, `progs.src` | Yes |
| Console config | `.cfg`, `.rc`, `.scr`, `.skin` | Yes |
| idTech3 shader | `.shader` | Yes |
| Entity definitions | `.def`, `.fgd`, `.ent` | Yes |
| Key/value scripts | `.ini`, `.arena`, `.bot`, `.menu`, `.conf` | Yes |
| JSON | `.json` | Yes |
| Map source | `.map` | Yes |
| C and C++, Python, Lua, ACS, ZScript, plain text and others | `.c`, `.cpp`, `.h`, `.py`, `.lua`, `.acs`, `.zsc`, `.txt`, … | No |

Files open in UTF-8 (with or without a byte-order mark) or UTF-16 with a
byte-order mark. Other encodings, binary files and files over 4 MiB open
read-only, with an explanation. Maps opened with **Go to File** or by dropping
them on the window go to **Levels**; the **Files** tab opens them here as text.

## Edit text

The editor indents new lines to match, and <kbd>Tab</kbd> and
<kbd>Shift</kbd>+<kbd>Tab</kbd> indent or outdent selected lines. Because Tab
indents, <kbd>Ctrl</kbd>+<kbd>Tab</kbd> and
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Tab</kbd> move the keyboard focus out of
the editor.

| Action | Keys |
| --- | --- |
| Toggle line comment | <kbd>Ctrl</kbd>+<kbd>/</kbd> |
| Duplicate lines | <kbd>Ctrl</kbd>+<kbd>D</kbd> |
| Move lines up or down | <kbd>Alt</kbd>+<kbd>Up</kbd>, <kbd>Alt</kbd>+<kbd>Down</kbd> |
| Fold or unfold a block | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>[</kbd>, <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>]</kbd> |
| Go to line | <kbd>Ctrl</kbd>+<kbd>L</kbd> |
| Go to symbol | <kbd>Ctrl</kbd>+<kbd>T</kbd> |
| Zoom the editor | <kbd>Ctrl</kbd>+<kbd>=</kbd>, <kbd>Ctrl</kbd>+<kbd>-</kbd>, or <kbd>Ctrl</kbd>+wheel |

You can also fold a block by clicking its marker in the line-number margin, or
use **View** > **Fold All** and **Unfold All**. **View** > **Sticky Headers**
keeps the first lines of the blocks you are scrolling through pinned above the
text.

## Find and replace

- <kbd>Ctrl</kbd>+<kbd>F</kbd> opens the find bar. <kbd>Enter</kbd> or
  <kbd>F3</kbd> finds the next match, <kbd>Shift</kbd>+<kbd>Enter</kbd> or
  <kbd>Shift</kbd>+<kbd>F3</kbd> the previous one, and <kbd>Esc</kbd> closes
  the bar. **Match case** narrows the search.
- <kbd>Ctrl</kbd>+<kbd>H</kbd> adds **Replace with**. **Replace**
  (<kbd>Enter</kbd>) replaces the current match and moves on; **Replace All**
  (<kbd>Ctrl</kbd>+<kbd>Enter</kbd>) replaces every match as one undo step.

To search the whole project:

1. Choose **Find in Project** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd>).
2. Type in **Find** and, if you like, set **Match case**, **Whole words**, and
   **Include** or **Exclude** file patterns such as `*.qc;*.qh`.
3. Choose **Search**. Results appear in **Search Results**; activate one to open
   its file at that line.
4. To replace, tick **Replace with**, type the replacement (leave it empty to
   delete the matches) and choose **Search** again to review every change
   before and after, then choose **Apply Preview…**.

Applying checks that no file has changed since the preview. Open files receive
ordinary unsaved, undoable edits; other files are saved directly in their
original encoding and line endings. Searches include unsaved text in open tabs.

## Navigate code

| Action | Keys |
| --- | --- |
| Go to Definition | <kbd>F12</kbd>, or <kbd>Ctrl</kbd>+click |
| Go back or forward | <kbd>Alt</kbd>+<kbd>Left</kbd>, <kbd>Alt</kbd>+<kbd>Right</kbd> |
| Find All References | <kbd>Shift</kbd>+<kbd>F12</kbd> |
| Complete Name | <kbd>Ctrl</kbd>+<kbd>Space</kbd> |
| Next or previous problem | <kbd>F8</kbd>, <kbd>Shift</kbd>+<kbd>F8</kbd> |

Without a language server, definitions come from a project index of names, so
they are textual; **Index Code** on the page header rebuilds it, and the
**Index** tab shows its results. Find All References then uses a whole-word
text search, and completion offers names from the file, the index and the
language's keywords. The **Problems** tab lists what VibeStudio's checks find
in the open file; problem lines are underlined, and the problem count above the
editor jumps to the next one.

## Save your work

**Save** (<kbd>Ctrl</kbd>+<kbd>S</kbd>) keeps the file's encoding, byte-order
mark, line endings and final newline, and **Save File As…**
(<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd>) writes a copy.

- If the file changed on disk since you opened it, VibeStudio asks before
  writing; **Cancel** is the default and keeps both versions.
- When another program changes an open file, a tab without unsaved edits
  reloads by itself, and a tab with unsaved edits asks first.
- A deleted file can be written again from its tab with **Recreate**.
- If the file is a map with unsaved edits in **Levels**, save or discard those
  edits first.
- Unsaved work is checkpointed every few seconds. **File** >
  **Recover Text Documents…** lists the copies and restores one as an unsaved
  draft.

Builds and packages use the files on disk, so save before you compile or stage
a script.

## Connect a language server (optional)

A language server, such as `clangd` for C and C++, adds live diagnostics,
symbol information, parameter hints, smarter completion, formatting and
renaming. VibeStudio does not include or install one.

1. Open the **Language Server** tab below the editor.
2. Choose the server program by its full path, then enter the language
   identifier and the file extensions it handles.
3. Add any arguments, one per line, under **Arguments and log**.
4. Choose **Connect**. **Disconnect** stops the server.

The server runs with your account's permissions and sees the matching files in
your project, including unsaved text, so choose one you trust. It never starts
by itself. When connected, these commands use it:

| Command | Keys |
| --- | --- |
| Quick Info | <kbd>Ctrl</kbd>+<kbd>I</kbd> |
| Parameter Hints | <kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Space</kbd> |
| Format Document, Format Selection | <kbd>Alt</kbd>+<kbd>Shift</kbd>+<kbd>F</kbd>, <kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>F</kbd> |
| Rename Symbol | <kbd>F2</kbd> |
| Code Actions | <kbd>Ctrl</kbd>+<kbd>.</kbd> |

Rename and code actions show every proposed change in **Search Results** before
you apply them. For the AI-based alternative, right-click in the editor and
choose **Ask Assistant About Selection** (see [AI assistant](ai.md)).

## Inspect shader scripts

Shader scripts and Doom 3 materials have their own page: **Materials** on the
rail (<kbd>Ctrl</kbd>+<kbd>8</kbd>) previews them live and edits them as text
or as nodes; see [Materials and shaders](materials.md). Its **Script Outline**
tab keeps the older tree view of one shader script: choose **Open Script**, or
type a path and choose **Inspect**.

The outline lists every shader with its stages, blend modes and the textures
it references. Textures missing from the open package are marked, so open the
game's or project's package first. The filter searches shader names, stages
and textures.

- Activate a texture row to show the texture, or a shader or stage row to open
  the script in **Code** at that line.
- Right-click a row for **Show Texture**, **Open in Code Editor**, and
  **Copy Shader Name** or **Copy Texture Path**.

The outline is read-only; edit scripts on the **Materials** tab or as plain
text in **Code**. `shader set-stage` on the command line changes one stage
setting and writes the result to a new file; the `material` commands cover
much more (see [Materials and shaders](materials.md)).

## Command-line equivalents

```sh
vibestudio --cli code index ./mymod --find monster --json
vibestudio --cli asset find ./mymod --find player --whole-word --include "*.qc;*.qh" --json
vibestudio --cli shader inspect ./scripts/common.shader --package ./baseq3 --json
```

<details>
<summary>All code and shader commands</summary>

| Task | Command |
| --- | --- |
| Index symbols and diagnostics | `code index` |
| List project source files | `code files` |
| Check a file's encoding and hash | `code text-info` |
| Create, save or copy a file safely | `code text-create`, `code text-save`, `code text-save-as` |
| List or restore recovery copies | `code recoveries`, `code text-recover` |
| Use a language server without the GUI | `code language-server` |
| Search or replace across the project | `asset find`, `asset replace` |
| Read a shader script and check its textures | `shader inspect` |
| Change one shader stage setting | `shader set-stage` |
| List entity classes, check a map's entities | `entity definitions`, `entity validate` |

</details>

The `code text-…` commands and `asset replace` only preview until you add
`--write`. See [Command line](cli.md) for details.

## Learn more

- [Code editor reference](../CODE_EDITOR.md): encodings, saving, recovery and limits.
- [Local language services](../LANGUAGE_SERVICES.md): setting up servers and what each feature does.
- [Project search](../PROJECT_SEARCH.md): scope, limits and partial writes.
