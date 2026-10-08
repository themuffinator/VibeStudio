# Packages

The Packages page opens game archives and folders so you can browse, preview, extract, check and
compare their contents, and build new packages from them. Every edit is staged first: the package you
opened stays unchanged until you save, and saving writes a new file unless you choose to replace one.

> [!NOTE]
> **Status: Available.** Browsing, previews, extraction, validation, comparison, staged edits and
> Save As work for PAK, WAD, ZIP and PK3 archives and for folders, with automated tests but little
> real-world use.
> Encrypted ZIP entries and compressed WAD2/WAD3 lumps are listed but not read, and very large
> packages have not been tested at scale.

## Supported formats

| Format | Open | Save | Notes |
| --- | --- | --- | --- |
| Folder | Yes | No | Save the staged result as an archive instead. |
| Quake PAK (`.pak`) | Yes | Yes | Always stored uncompressed. |
| Doom IWAD and PWAD (`.wad`) | Yes | Yes | Keeps the lump order, including repeated map names. Lump names are at most 8 characters. |
| Quake WAD2, Half-Life WAD3 (`.wad`, `.wad2`, `.wad3`) | Yes | Yes | Compressed lumps are listed but not read; saved lumps are uncompressed. |
| ZIP and PK3 (`.zip`, `.pk3`, also `.pk4`, `.pkz`) | Yes | Yes | Stored and DEFLATE entries. Encrypted entries and other compression methods are listed but not read; multi-disk archives are refused. |
| Package draft (`.vibepackage`) | Yes | Yes | Your staged edits and their history, described below. |

## Open a package or folder

- Choose **Open Package** on the Packages header, or **File** > **Open Package…**
  (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>O</kbd>), and pick an archive.
- Choose **Open Folder**, or **File** > **Open Folder Package…**
  (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>O</kbd>), to treat a folder on disk as a package.
- Drop an archive onto the window, or reopen one from **File** > **Open Recent**.

On macOS, press <kbd>Cmd</kbd> wherever this page says <kbd>Ctrl</kbd>. Opening runs in the
background with progress and **Cancel**, and the Activity Center records the result. A package with
more than 250,000 entries, including folders, is refused rather than half loaded. **File** >
**Close Package** (<kbd>Ctrl</kbd>+<kbd>W</kbd>) closes it.

**New Package** starts an empty document instead. Choose its format: **PK3 package**, **ZIP archive**,
**Quake PAK**, **Doom PWAD**, **Doom IWAD**, **Quake WAD2** or **Half-Life WAD3**.

## Browse the contents

The page has three columns. **Folders** on the left is the package's folder tree. The entry list in
the middle shows the current folder, with back, forward and up buttons and a clickable folder path
above it. The inspector on the right has four tabs:

- **Details**: what is known about the selected entry, such as image size, model frames or sound format.
- **Preview**: the decoded pixels of an image, sprite or texture, or the text of a text entry.
- **Staging**: your staged changes, described in [Stage changes](#stage-changes).
- **Overview**: **Composition By Type**, a chart of how the package divides by type and size.
  Select a slice to filter the list to that type.

Double-click an entry, or press <kbd>Enter</kbd>, to open it where it belongs: images on Textures,
models on Models and sounds on Audio. Maps, shader scripts, entity definitions and text files open
from a temporary copy; text opens read-only on the Code page, because edits to a copy never reach the
package. Right-click an entry for **Open**, **Extract Selected…**, **Stage Replace…**,
**Stage Rename…**, **Stage Delete**, **Unstage** and **Copy Virtual Path**.

Previews are prepared in the background. **Cancel Preview** stops one and **Retry Preview** reads it
again. Large entries are previewed from a sample, which does not prove the whole entry is intact; use
[Validate](#check-package-integrity) for that.

## Filter the entry list

Type in the filter field above the list (its placeholder reads "Filter entries by path or type"). A
plain word keeps entries whose path or type contains it. Property tests narrow the list further, and
every term you type must hold.

| You type | Keeps entries where |
| --- | --- |
| `ext=wav` | the property equals the value, ignoring case |
| `folder:textures` | the property contains the text |
| `ext!=tga` | the property does not equal the value |
| `size>1mb`, `size<=64k` | a number compares; sizes take `k`, `kb`, `m`, `mb`, `g` or `gb`, counted in 1024s |
| `name:"stone wall"` | quotes hold a value with spaces |

The keys are `path`, `name`, `ext`, `folder`, `type`, `storage`, `kind`, `size` and `packed` (the
compressed size). Types read like `image/png`, `audio/wav` or `text/shader`, and Doom WAD lumps use
types such as `wad-flat`, `wad-sprite`, `wad-patch` and `wad-sound`, so `type:image` keeps every image.

Press <kbd>Enter</kbd> in the filter to select every match and move to the list, where extraction
and staging act on the whole selection. Press <kbd>Down</kbd> in the filter to reuse an earlier
query. The same syntax works in `vibestudio --cli package list <package> --where "<query>"`, and in
other lists such as **Objects** on the Levels page.

## Extract files

1. Select entries in the list.
2. Choose **Extract** on the toolbar (<kbd>Ctrl</kbd>+<kbd>E</kbd>), or **Project** >
   **Extract All Entries…** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>E</kbd>) for everything.
3. Choose an output folder.
   A dialog shows the current file and its progress.

Extraction never overwrites existing files: they are skipped. Unsafe names, such as absolute paths or
`..`, and links in the output path are refused. When two selected entries would land on the same
name, as repeated WAD lumps do, **Extraction Paths** lets you give each a separate path.
**Cancel Extract** stops at once: files already extracted stay on disk and the unfinished one is
discarded. You can also drag a few entries out of the list into another folder or program; use
**Extract** for large batches.

## Check package integrity

Choose **Validate** on the Packages header. VibeStudio reads every entry, including each copy of a
repeated WAD lump, checks its size and checksum, and records a SHA-256 hash. A package passes only
when every file verifies with no warnings. **Export JSON…** saves the report. Validation checks the
container, not whether each asset works in the game.

## Compare two packages

Choose **Compare** on the Packages header and pick another archive. VibeStudio pairs entries by path,
ignoring case, and reports added, removed, changed, case-only and unchecked entries; files of equal
size are hashed to prove they match. Search the results, select a row for its paths and hashes, or
choose **Export JSON…**. Comparison and validation both run in the background and can be cancelled.

## Stage changes

Staged changes build a plan for the next save. The list and folders show them at once, but nothing is
written until you save.

| To | Do this |
| --- | --- |
| Add a file | **Project** > **Stage Add File…** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>A</kbd>), then choose the file and its path in the package. |
| Replace an entry | Select it, then **Project** > **Stage Replace…** (<kbd>Ctrl</kbd>+<kbd>Alt</kbd>+<kbd>R</kbd>). |
| Rename an entry | Select it, then **Project** > **Stage Rename…** (<kbd>F2</kbd>). |
| Delete entries | Select them, then **Project** > **Stage Delete** (<kbd>Del</kbd>). |
| Make or change folders | **Project** > **New Package Folder…**, or right-click a folder for **Rename Folder…** and **Delete Folder**. |
| Rename or remove Doom maps | **Project** > **WAD Groups…**, which keeps each map's lumps together. |
| Undo or redo | **Edit** > **Undo Package Edit** (<kbd>Ctrl</kbd>+<kbd>Z</kbd>) and **Redo Package Edit** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>Z</kbd>). |

The same commands sit as icon buttons in the toolbar's **Stage** group; hover over one to see what it
does. The keys act only while the Packages page has focus. VibeStudio keeps its own copy of each file
you add, so changing the original afterwards does not change the staged entry. ZIP and PK3 keep empty
folders. A PAK cannot store them, so saving a PAK with an empty folder is refused. WAD has no folders.

The **Staging** tab lists every change with its conflicts and blockers; right-click one for
**Unstage** or **Show Entry**. Before saving, choose **Review Changes** to compare the staged result
with the source, entry by entry, with the same search and **Export JSON…** as **Compare**. Renaming
or deleting an entry does not update references to it in maps, scripts or shaders, so check those
before you publish.

## Save a new package

1. Choose a **Compression** level: **Store (no compression)**, **Fast**, **Default** or **Best**.
   It applies to ZIP and PK3; PAK and WAD are always stored.
2. Choose **Save As** (<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>S</kbd>) and enter a file name ending in
   `.pk3`, `.zip`, `.pak` or `.wad`. The extension chooses the format.
   A dialog shows progress and offers **Cancel**.
3. When the save finishes, the new archive becomes the open package. An open draft stays open
   instead, with its history.

**Include Staging Manifest**, in the menu beside **Save As** and on by default, also writes
`<name>.manifest.json` with the hashes of every original and final entry. A failed or cancelled save
keeps your staged changes so you can try again.

### Replace an existing package

Saving over a file that already exists, including the open package, is opt-in. VibeStudio asks
**Replace Existing Package?** first. If you agree, it writes the new archive beside the old one,
verifies it, and only then moves the original to `<file>.bak`. If a save is interrupted, **File** >
**Recover Packages…** > **Interrupted Saves…** checks the files left behind. When the new package is
already in place and verified, **Finish Reviewed Save…** completes the backup and cleans up.

## Keep work in progress

**File** > **Save Package Draft** (<kbd>Ctrl</kbd>+<kbd>S</kbd> on the Packages page, also an icon on
the toolbar) stores the source content, your staged changes and their undo history in a `.vibepackage`
folder. **File** > **Open Package Draft…** resumes it later, even without the original files; move or
copy the whole folder. VibeStudio also keeps local recovery checkpoints every 30 seconds by default,
and **File** > **Recover Packages…** restores one as a new draft.

## Export part of a package

Select files or folders and choose **Export Selected** on the Packages header to save them as a
separate package. Doom map lumps travel with their whole map. To collect the textures, models and
sounds a map uses, open the map and its package and choose **Dependencies** on the Levels header,
which can export the resolved assets; see [Level editing](levels.md).

## Command-line equivalents

| Task | Command |
| --- | --- |
| Summarise or list | `vibestudio --cli package info <package>`, `package list <package> --where "<query>"` |
| Preview one entry | `vibestudio --cli package preview <package> <entry>` |
| Extract | `vibestudio --cli package extract <package> --output <folder>` |
| Validate | `vibestudio --cli package validate <package>` |
| Compare | `vibestudio --cli package compare <left> <right>` |
| Preview staged edits | `vibestudio --cli package stage <package> --add-file <file> --as <entry>` |
| Save a new package | `vibestudio --cli package save-as <package> <output> --format pk3` |
| Start an empty package | `vibestudio --cli package create <output> --format pk3` |
| Save part of a package | `vibestudio --cli package subset <package> <output> --prefix <folder>` |

For example:

```sh
vibestudio --cli package list ./baseq3/pak0.pk3 --where "type:image size>256k"
vibestudio --cli package validate ./release.pk3 --json
vibestudio --cli package extract ./pak0.pak --output ./out --entry maps/start.bsp --dry-run
vibestudio --cli package compare ./release-1.pk3 ./release-2.pk3 --json
vibestudio --cli package save-as ./mod-folder ./build/mod.pk3 --format pk3 --add-file ./autoexec.cfg --as scripts/autoexec.cfg
```

`package validate` and `package compare` exit with code 4 when they find a problem or a difference.
`package save-as` refuses to write over its source unless you pass `--in-place`, which keeps the
original at `--backup` (by default `<output>.bak`). Add `--dry-run` to see what a write would do.

## Learn more

- [Package manager](../PACKAGE_MANAGER.md): limits, drafts, recovery and every staging rule.
- [Support matrix](../SUPPORT_MATRIX.md#archive-and-package-formats): exact format support.
- [CLI strategy](../CLI_STRATEGY.md): every `package` option.
