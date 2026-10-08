# Initial Setup Flow

**Help** > **Documentation**, also available in the Command Palette, opens the
bundled HTML manual without setup. Release packages support offline reading;
development builds or installations without the manual open the online manual.

**Multitrack → Record Tracks…** requests microphone permission only on Record.
Choose current input/output devices, a new `.vsrecord` folder, timing and up to
eight armed tracks. Monitoring starts off; no device or arming choices are
restored as active recording. Loop passes defaults to one; increasing it repeats
the punch range with one preroll and continuous monitoring/effects. Review's
Recorded pass selector chooses an available pass for each arm without opening
input. Pass choices and trims apply to the current review. Assemble comp sections
is off by default; enable it to queue ranges with Add/Update/Remove and optional
after-cut crossfades. Zero frames makes hard cuts. Use queued take selections is
off by default and enabled by comp assembly or opening a saved review. Save
Review/Save Review As retain the active choices in portable JSON; Open Saved
Review restores the complete queue after verification. Changed choices prompt
Save/Discard/Cancel before closing or replacing the review. Save does not import or
start playback. Import creates one undoable set of editable clips without
changing journals. Review recipes are separate files, not native session lanes.
Audition Current Section/Review verifies the chosen audio before import. The
Audition tab defaults to backing included, system-default output, a 2048-frame
buffer, 70% listening volume and repeat off. It has no microphone permission or
account requirement. Edits stop stale playback; recording/import/close wait for
output shutdown. CLI preview works with playback disabled and saves float32 WAV
using the same review and guards. No audition preference is restored as active playback.
Browser, waveform and
session playback must
acknowledge shutdown before input starts. Review/import needs no account,
AI connector or audio permission; it also works in device-free builds. CLI
`asset audio-recording inspect` and `import` use the same verification and
session services. Standalone capture remains under **Single Take…**. No first-run
recording switch is added. The Meters tab needs no additional setup and opens
with the pass; it shows dry input and pre-clamp output levels without enabling
monitoring. Reset affects meter history only. Final readings are retained in
the current dialog, not saved in the pass folder. Physical platform acceptance
remains open; see
[recording setup and limits](AUDIO_EDITOR.md#recording-and-recorded-takes).

Session **Meters…** needs no additional setup. Live readings follow Play's
selected output; **Analyze Range** works without Qt Multimedia or device
permission and uses the session's Range start/end. Signal point changes and
meter reset do not edit the session. Playback starts/seeks/stops reset readings;
pause and loop repeats retain them. Numerical levels and counts accompany
native level bars. Session Loop needs no extra setup: sources/automation repeat
while effects continue, and compensation primes once. Changing the loop policy
resets processing at the consumed cursor; pause and meter reset preserve it.

Session media management needs no account, device permission or additional
setup. Choose **Media…**, select a source and operation, then **Review** and
**Apply**. Missing external files do not disable embedded audio. Relink requires
identical decoded samples; Replace affects every clip using the source and
requires explicit rate conversion when needed. Remove/Prune affect unused
embedded snapshots only, preserve files on disk, and use session undo/recovery.

Time-range editing needs no setup or device permission. In a multitrack session,
set Range start/end frame and choose **Range…** to clear clips, close a deleted
gap, insert silence or repeat a section. Check exact tracks or All tracks;
group links do not expand that scope. Time edits follow track automation by
default, with a separate master choice available for all tracks. Clear leaves
automation in place. Tempo/meter markers remain unchanged. Apply stops playback
and makes one recoverable undo change; Cancel preserves the session.

Clip grouping requires no additional setup. Select clips across tracks, then
use **Selection…** for grouped edits. **Link grouped clips** initially includes
all members of selected groups; the target count previews that expansion.
**Clip…** edits only the focused clip. Splits preserve their audible fades;
new saves use native v7 and older v1–6 sessions remain readable. These edits
stop playback and use normal undo/recovery without opening an audio device.

Tempo/meter editing needs no device, plugin or network setup. In a multitrack
session, use **Tempo / Meter…**, **Bars and beats** in Ruler, and the musical
Position field. Snap follows the current tempo and time signature. Map edits
are undoable and keep audio/automation at their sample positions. Versions 1–4
open with their original constant timing; new saves use version 7. No metronome,
recording, time stretching or external synchronization starts implicitly.

Lookahead limiting and automatic processing latency compensation need no setup,
device permission or extra dependency. Add the lookahead limiter through a
track/bus Effects inspector or Master Effects. The displayed insert latency
updates with its staged lookahead value; Apply rebuilds alignment and stops
playback. This does not calibrate an input/output device or enable monitoring.

Stereo buses and sends require no additional setup, plugin or device permission.
In a multitrack session, add a bus and use Routing / Sends to connect strips.
Existing version-1 sessions open with their original direct-master routing;
saving writes version 7. New GUI sends begin disabled until explicitly enabled.
Playback stops before applying a routing edit, and normal undo/recovery retains it.

Effects… edits the selected track/bus insert chain; Master Effects… edits the
stereo output. Native processors need no setup, account, network or plugin scan.
Apply stages one undoable change and stops playback. The saved Session tail
setting controls the default range after enabled filters/delays; output-device
preferences remain separate. See [Session Effects](AUDIO_EDITOR.md#session-effects).

Reverb, chorus, flanger, tremolo, phaser and nine factory presets are available
without downloads. Expand Presets in the effects inspector to load a recipe or
open/save `.vsfx` files. Loading stages a replacement chain; Apply updates the
session. Presets preserve parameter values and reject incompatible sample rates;
their suggested tail never shortens an existing session tail automatically.

Gain/pan and effect Automation dialogs are available without extra setup.
Effect curves use Read automation (or off), absolute session frames and the
same point editor as track envelopes. Applying a draft stops playback and is
undoable. Sessions retain curves in version 7; older versions migrate their
linear envelopes. Presets contain static values and replacement clears the
selected chain's old lanes. No device or automation-write mode starts implicitly.

Recording is opt-in for each take under **Audio → Multitrack → Record / Takes…**.
Choose a new file, an explicit input device and channel map, then arm and press
Record. Startup/setup, reopening a session and arming never open a microphone.
Input, buffer and measured compensation choices are per dialog; active recording
is never restored. Denied OS permission leaves review/editing available. macOS
requires Qt 6.5+ microphone permission support and a correctly declared/deployed
application bundle; that native packaging/permission acceptance remains open.
Saved `.vstake` files are independent user documents: reopen them through Open
Audio for verified-prefix recovery. The checkpoint startup inventory does not
automatically scan recording folders. See [Recording](AUDIO_EDITOR.md#recording-and-recorded-takes).

Asset authoring is available immediately after setup: choose Texture Editor in
Textures, Mesh Editor in Models, or Open Audio in Audio. These commands also appear
under Tools and in command search; no game installation, compiler or AI connector
is required. Open a package or asset folder to browse existing content. The asset
header's Package menu returns to its source entry, reviews pending changes or saves
the package draft. Save each authoring document separately and use Save Workspace
As to preserve project and module references for the next session.

The editor-profile step and Settings expose 24 familiar editing schemes,
including GtkRadiant 1.4, 1.5 and 1.6, QeRadiant, Q3Radiant, standalone NetRadiant,
NetRadiant Custom, TrenchBroom, QuArK, Hammer/Worldcraft, J.A.C.K., Sledge, DarkRadiant, Doom Builder variants, SLADE,
Eureka, DoomEdit, BSP and modern scene editors. **Browse Editor Profiles…** in
the Levels Controls menu searches names, aliases and engine families and previews
default controls and adaptations before **Use Profile** applies a choice.
The Levels Controls menu changes the same
setting immediately. Controls help lists supported gestures and searchable
adaptations; choosing a scheme does not change the game target, map format,
installation permissions or explicit layout preference. It does arrange and
name the Levels sidebar tabs the way that editor family does, keeping any tabs
you have moved for that family; **Reset Sidebars** returns to its defaults. CLI aliases such as
`editor select hammer++`, `editor select udb`, `editor select NRC`, `editor select TB`
and `editor select "GtkRadiant 1.5.0"` persist a canonical profile ID.
See [Editor Profiles](EDITOR_PROFILES.md).

After choosing a profile, **Customize Gestures…** in the same Settings category
or the Levels Controls menu adapts pointer, fly/drive and mouse-look bindings without another setup step.
Changes apply only to that profile. Defaults, conflict feedback and portable
import/export are available in the dialog; Keyboard settings continue to manage
command shortcuts. Possible camera/command overlaps have expandable details.
Applying gesture preferences preserves the current workspace.

The modeller has its own controls profiles (VibeStudio, Blender, 3ds Max and
MilkShape 3D). Until one is chosen on the Mesh Editor's **View** page or with
`model controls --select`, it follows the editor profile picked here: the
Blender level profile gives the Blender modeller profile and every other
profile gives VibeStudio's own. See [Modeller Profiles](MODELLER_PROFILES.md).
Q3Radiant is separate from GtkRadiant and NetRadiant. Its classic position
steering, fixed camera steps and independent preferences need no additional
setup. The same gesture dialog can remap or disable steering and fixed steps.
Sledge defaults Space to temporary plan pan/camera look; release restores the
previous navigation mode. Hold keys, camera pitch and arrow behavior are
customizable in the same dialog. Selecting standalone NetRadiant keeps its
preferences separate from NetRadiant Custom. Both expose their differences
in Controls help without another installation or setup requirement.
GtkRadiant 1.4 and 1.5 retain separate gesture preferences: 1.4 uses Alt area
selection, while 1.5 uses Shift. Both provide discrete camera steps with A/Z
pitch outside free look. QeRadiant provides its classic texture-fit keys without
Q3 patch shortcuts. Each preset describes its adaptations; no upstream native
preferences, component modes or extra map formats are imported.

The Levels Layout menu can temporarily maximize the focused view and restore
its previous pane sizes. This needs no setup and does not change the chosen
layout preference. Equalize View Sizes resets only the current arrangement's
splitter proportions. Profile help and keyboard settings expose these commands.
Viewport status labels adapt automatically to narrow panes, enlarged text and
RTL layout; no separate display preference is needed.

Model collision boxes need no additional setup, game installation or AI connector.
Collision component mode enables edge/table selection and the existing transform
tools. Move and rotation use Geometry's chosen transform axes; scale uses box-local axes. Geometry's
numeric fields provide the same operations and snapping. Boxes remain static
across animation poses.
Open the Mesh Editor's Collision tab and save the mesh source. Choose Quake,
Quake II or Quake III explicitly before map export or level placement; Quake III
requires your project clip shader in the compiler assets. Save and compile the
level after placement. Boxes remain independent of later prop edits. See
[Model Collision](MODEL_COLLISION.md) for target semantics and CLI use.

Offset placement and snapping work offline without a compiler or AI connector.
They use the Levels Texture Lock preference. Open an asset folder/package to
preview materials and placed models; the current package draft supplies staged
assets. **Duplicate with Offset…** and **Paste with Offset…** are in Edit and the
map context menu. Save the map before compiling and review its dependencies
before publishing a package. See [Placement and grid alignment](LEVEL_EDITOR.md#placement-and-grid-alignment).

Quick Snap, Duplicate, Paste and Mirror prepare on a worker. Longer edits show progress
and Cancel; cancellation leaves the map unchanged. Offset previews also report
their geometry, asset and dependency phases. Reduced-motion preferences apply
to these progress indicators without additional setup.

For binary Doom/Hexen geometry, **Select Connected Geometry** explicitly expands
the selection before a horizontal/vertical flip when shared vertices attach it
to unmoved boundaries. Things remain independently selected. This works offline
without additional setup. Save the WAD and rebuild nodes using a configured node
builder before testing; native wall offsets are retained. Existing UDMF maps
offer **Edit → UDMF Properties…**, common-field previews, scene organization and
standard move/rotate/mirror/snap/resize and thing duplication without extra setup.
**Duplicate with Offset…** supports fractional XYZ offsets and repeated copies.
Save and rebuild
nodes after geometry or raw property edits; ordinary-thing transforms retain
valid nodes. Copying polyobject control things also requires a rebuild. UDMF
geometry duplication, topology creation/deletion and advanced effects remain open.
See [Level Editor](LEVEL_EDITOR.md).

Levels scene organization needs no game installation, compiler, account or AI
connector. Open a supported map and use its Scene tab. Create in is a per-session
destination; saved layers and groups travel inside the map through Save As,
recovery and packaging. Unrecognized or stale metadata is preserved and explained
in Scene status; Reset is explicit and undoable. See [Level scene organization](LEVEL_SCENE.md).

Scene locks also require no setup. Lock editing protects members and nested
groups across GUI/CLI authoring and asset placement. Unlock the supplying node
before editing or moving its members; builds, packages, inspection and copying
remain available. Lock state travels with the map and supports Undo/Redo.

Map opening needs no new setting, provider or tool installation. Open/Reload
shows phase progress and cancellation while the existing document is retained.
For WADs, marker choices appear after the first successful load; a marker can
also be entered explicitly. See [Background Map Opening](LEVEL_EDITOR.md#background-map-opening).

For Doom/Hexen camera editing, open the resource WAD or asset folder in Packages
and then the map in Levels. `PLAYPAL`, flat namespaces, `PNAMES`, texture tables
and patch images come from that package snapshot, including staged changes.
Missing inputs and generated palette fallbacks are shown in Details. No game
assets are bundled, and installed resources are not automatically merged.
Floors, ceilings and wall parts share Paint/Sample and the same save/undo flow.

Material painting needs an editable map; a package or asset folder supplies
images through the offline material resolver shared by Models and Textures.
Choose Paint or Sample in Levels, or Use for Map Painting in Textures. Explicit
Targets support keyboard editing and Doom flats. No additional settings,
service or dependency is required. Radiant-family profiles also offer middle-click
material-name sampling; QeRadiant, Q3Radiant and GtkRadiant 1.4/1.6 use Shift+middle for one-surface
painting. Brush sampling also copies mapping and flags. Ctrl+middle pastes onto
the hit brush and Ctrl+Shift+middle onto the hit face in these profiles.
GtkRadiant 1.5 keeps middle sampling and Ctrl+Shift+middle hit-face paste,
with Shift+middle and Ctrl+middle unbound.
Gesture Preferences can reassign or disable all four actions. Existing navigation
and explicit sample/paint choices retain priority over new paste defaults.
Native upstream preference-file import remains separate work. See
[Material Painting](LEVEL_EDITOR.md#material-painting).

[Saved level views](LEVEL_EDITOR.md#saved-level-views) need no setup or network
connection. They live under application configuration, or beside an explicit
settings file in its `.level-views` directory. Each map and WAD map marker has
its own list. Unsaved maps retain views for the session and persist them when
saved; portable export supports sharing and backup independently of game files.

Levels' **Layout** choice starts at **Follow Editor Profile**. Users can choose
four views or another arrangement without changing familiar gestures and keys.
The preference and each layout's splitter sizes persist locally; no extra
dependency or online service is needed. CLI `editor layout` shares this setting.
See [Four-View Workspace](LEVEL_EDITOR.md#four-view-workspace).

Plan centres, plan zoom and camera-follow start independent. Enable each in
Levels' Layout menu or configure the same defaults with `editor view-links`.
Settings persist locally without additional dependencies. Saved views can
temporarily apply different links; opening another map restores the defaults.
See [Linked Navigation](LEVEL_EDITOR.md#linked-navigation).

Prefabs require no additional dependency, provider or online service. Open a
Quake-family map to capture or place an assembly, and an asset folder/package
for material/model preview and dependency review. A package draft is required
for **Stage Selection as Prefab**. Placement inherits the editor's texture-lock
preference; it never silently converts brush dialects. Save the map and publish
required assets before compiling. See [Reusable Prefabs](LEVEL_EDITOR.md#reusable-prefabs).

Camera entity placement needs no setup or provider. Select a class in **Create**,
show the camera and choose **Place at Camera Surface** while aiming at geometry.
Loaded entity definitions supply placement bounds; the fallback marker is
8 units in each direction. **Clearance** is a session-only extra gap. The current
grid and **Create in** layer apply. Doom/Hexen things require a sector floor and
ignore clearance; UDMF creation remains planned. See
[camera surface placement](LEVEL_EDITOR.md#camera-surface-placement).

Camera brush creation needs no setup or provider. Open or create a Quake-family
map, choose **Draw Brush**, a construction plane and a material. The current
grid/snap and remembered selection depth apply; Base/Depth and **Use Work Zone**
offer exact control. Drag a footprint, adjust depth with the wheel, and release.
**Numeric Brush…** provides the existing keyboard primitive preview. Scene
creation layers, locks, undo, recovery and save remain shared with the plan
tools. See [camera brush creation](LEVEL_EDITOR.md#camera-brush-creation).
Plan/camera insertion prepares on a worker and shows Cancel for longer edits.
Numeric Apply reuses the validated preview; changed map, selection, destination
or package context requires reopening the draft. No additional setup is needed.
The Objects list and background filter also need no setup. Clearing the query
cancels filtering; Enter can select its result once ready. Changing the query
or opening another map cancels that pending selection. Full row details remain
in tooltips and accessibility text when the compact list elides them.
Map material counts and brush suggestions update automatically after edits;
blank names and Doom's no-texture marker are omitted from suggestions. No
material-index setup or service is required.

Camera selection resizing needs no setup or provider. All profiles expose
labelled face handles for Quake-family selections and XY spacing for Doom
things; **Resize Selection** provides numeric keyboard editing. Handles use
the shared grid/snap and resize texture policy. Loading, camera or source
changes cancel pending gestures. Save the applied map before build/package
handoff. See [camera resizing](LEVEL_EDITOR.md#camera-selection-resizing) for
Doom topology and live texture-preview limits.

Level transforms need no additional setup, provider or dependency. Texture Lock
defaults on for moves, quick turns and flips; Texture Scale Lock defaults off for
resizing. The native Edit actions persist these preferences. Allow Valve 220
Conversion defaults off: enable it only for a compiler that accepts that dialect.
Numeric rotation inherits these settings when opened. Save authored maps before
dependency review, compiler orchestration and package publication; changing the
editor's texture policy does not rewrite material images or model assets.

Cap Patch needs an existing curved boundary in a Quake III map. It requires no
new setup, cloud service or dependency. Open a package/asset folder for textured
preview, including staged replacements; cap geometry authoring also works offline.
Save the map before dependency review, compiler orchestration or package export.

Plan selection acceleration is automatic across editor profiles and layouts;
there is no new setting or dependency. Frame Selection includes point entities,
vertices and straight lines, while resize/work-zone bounds retain hidden geometry
owned by selected entities. Bookmarks and linked views keep their existing setup.
Shared plan snapshots, brush geometry reuse and scene visibility acceleration
also apply automatically across profiles, without changing controls or setup.
Details also keeps the selected section across map edits and selection changes.
Unchanged detail text retains its reading position; no preference is required.
Packages also preserves the current preview while selecting additional entries
and keeps the chosen Details section when background preview work completes.
Package controls restore their availability after work completes, fails or is
cancelled. Selection changes preserve unrelated studio selections; no setup or
preference is needed.

Plan brush rendering also accelerates automatically, using the same CPU wire
renderer as model previews. It follows pane size, display scaling and contrast
preferences without a graphics API, driver requirement or additional setting.
Grid, selection, labels and editing previews retain the current profile's behavior.
Selected brush and patch outlines follow the same display scale and contrast
preferences automatically in every plan profile. Entity selections highlight
their visible owned geometry. No new setup control is required.
Larger Quake scenes automatically prepare plan images in the background and show
**Updating view…** until ready. Navigation keeps a transformed image of the prior
view during that interval; the primary selection and editing handles stay live.
Small scenes and physical targets above the image budget retain immediate
complete painting. No worker, graphics or cloud connector setup is needed.
Grid and member-marker preparation is also automatic and uses the background
path for larger Quake scenes or selections above 64 objects, including Doom.
The updating status stays visible until geometry and overlays are current;
selection changes immediately clear outdated highlights. It follows display
scale and contrast, with complete ordinary drawing beyond cache limits. Primary selection
labels stay near the selected object, inside the pane and clear of its status
tags; long names elide visually while remaining complete for assistive tools.
Fractional display scaling also follows each split pane's position automatically,
keeping cached grid and geometry strokes aligned with selection markers and
editing handles as layouts move. It requires no profile or setup adjustment.

Stitch Patches requires two existing patches in a Quake III map and no new setup
or AI connector. Open a package/folder to preview its material images and staged
replacements. Geometry and CLI stitching work offline without those images.
Shared theme, scaling, palette and reduced-motion preferences apply. Save the
map before the compiler/package handoff; see [Patch Stitching](LEVEL_EDITOR.md#patch-stitching).

Merge Brushes requires no additional setup, AI connector or external geometry
tool. Its before/after views resolve the open package/folder and staged materials
through the shared Models renderer; geometry review also works without images.
Surface conflicts require a source choice before one undoable map edit. Save
the map before compiler/package handoff. See [Brush Merging](LEVEL_EDITOR.md#brush-merging).

Add Brush now previews box, wedge, cylinder, cone and sphere primitives using
the same package/staging materials and Models renderer as the level camera.
Geometry authoring works without asset images. No extra tool or AI connector is
required. Save the map before compiling or packaging it. See
[Brush Primitives](LEVEL_EDITOR.md#brush-primitives).

Surface Alignment uses the open package or folder for original material sizes.
The adjacent **Surfaces** tab provides nonmodal shift, rotation, size, fit and
centre controls, with an explicit selection or inspected-face target.
Copy/Paste Surface shares this target and a session-local material/mapping/flag
clipboard. Full matching-format parameters work without images; World projection
and Seamless wrap use actual package dimensions when converting units. One-face
wrapping advances the clipboard for another corner; a multi-face batch retains
the original source. NetRadiant uses Shift+middle for face parameters; NetRadiant
Custom uses Ctrl+middle for a one-face wrap and Shift+middle for hit-and-selection
values, including patch materials. Alt preserves target materials/flags for both
gestures. Radiant values keeps Valve axes; mapping-only primitive transfer uses
source and target image sizes. Copy captures its package context across later
package switches. No image loading blocks the GUI. Explicit map-wide Valve
220 permission covers required shear while preserving unpasted UVs; locks apply
to every converted face. See
[Surface clipboard](LEVEL_EDITOR.md#surface-clipboard) for portable CLI files.
Radiant projection is available in every profile. NetRadiant Custom adds
Ctrl+Shift+middle for the hit and selected brushes/patches, plus Alt to retain
materials/flags. Older explicit bindings take precedence over these defaults.
Hold Values, Project or Wrap to cross several surfaces with a live preview;
release commits one undo step. Only the first hit includes the selection, and
changing modifiers switches the subsequent transfer mode. Wrapping follows the
last face and its package dimensions. Escape or Cancel Stroke discards the whole
gesture, including queued work after release. No cloud connector is required.
Patch projection needs its destination image dimensions; perpendicular brush
faces can become edge-on and are reported after the edit. No additional setup
or connector is required.
Step values are session-local; Q3Radiant's texture keys use these same values.
Queued edits have status and cancellation, remain unsaved and use map undo.
An explicit texture-size override supports offline editing when assets are not
available. It shares theme, text scale, palette and reduced-motion preferences;
there are no new setup steps or dependencies. Save map changes before the next
compile/package operation. See [Surface Alignment](LEVEL_EDITOR.md#surface-alignment).

Level material previews work offline with the open package or asset folder and
its staged edits. Open the project's content explicitly; automatic merging of
project roots and installation packages is not implemented for the camera.
The camera's Textures choice persists between sessions. Palette selection,
theme, text scale and reduced motion reuse existing studio settings. No new
dependency, AI connector or game launch is required. See [Level Editor](LEVEL_EDITOR.md).

New Package creates an untitled PAK, ZIP, PK3 or WAD document without a source.
It offers the same Save/Discard/Cancel decisions even when empty. Folder
creation/rename/delete use existing package history; WAD disables folder actions.
Package drafts work offline with no connector or additional setup. Save Package
Draft chooses a `.vibepackage` directory containing independent payload copies
and undo/redo history; subsequent saves reuse it. Recent files and session restore
reopen the draft directory. Save/Discard/Cancel protects modified drafts when
closing or changing packages. Drafts use existing theme, scaling, localization,
and reduced-motion preferences. Local recovery checkpoints are enabled by default
at a 30-second interval. File > Recover Packages reviews copies, restores to a new
draft, and controls the setting and 5–600 second interval. The browser reports
checkpoint progress/errors. Recovery defaults to 8,192 MiB and 32 copies; the
chooser shows usage, adjusts limits and discards reviewed incomplete copies.
Reaching a limit or disabling checkpoints retains existing copies. No additional
dependency or connector is required. See [Package Manager](PACKAGE_MANAGER.md).

Archive saves need no new setup or compression library. Their progress dialog
shows source checks, compression measurement, writing and publication, with
bytes processed within the current file. Cancel stops reads/compression before
publication; the final result reports whether a commit already completed.
Package comparison and staged review show the side, current path and per-file
bytes with cancellable reads. They use existing theme, language and scaling
preferences and require no additional setup.

**File > Recover Packages > Interrupted Saves…** reviews interrupted archive saves
in remembered output folders, the current project/recent package folders, or a
chosen folder. Actual GUI/CLI archive saves remember the latest 16 output folders
before writing; dry runs do not. Verification and finishing run on cancellable
workers. External backups require selecting their recorded path. The chooser
uses existing theme, scaling and language preferences; no connector is required.

Saved drafts have separate per-directory save limits of 32,768 MiB and 200,000
files. **File > Recover Packages > Saved Draft Storage…** adjusts them and reviews
unused objects in a chosen draft. Ranges are 128–131,072 MiB and 1–250,000 files.
Lowering limits retains content; unchanged saves need no extra space. Changed
saves include unused storage and room for the next manifest. Close the draft and
wait for its readers before confirmed reclamation. GUI and CLI use the same
preferences, verification and reader checks; cleanup preserves undo/redo.

Accepted file imports keep independent working copies through undo and background
reads, even if the original file changes or disappears. Qt's configured temporary
directory must be writable and have room for the imports. Copies are released
when the last document/history/reader reference ends; save a draft or use recovery
checkpoints for restart persistence. File > Recover Packages > Working Import
Storage… reviews reserved/current usage, adjusts limits and discards selected
abandoned sessions after checking their lease and reviewed storage.
**Review Lock Files…** can release an unchanged interrupted lock after its
owner exits, including empty locks; live locks are refused. This keeps all
payloads for separate session review. Normal shutdown waits for queued import
cleanup; failed cleanup retains the session for review. CLI storage/limit
inspection preserves missing or legacy settings without schema writes.
No additional setup or dependency is required. Defaults are
8,192 MiB and 50,000 files; ranges are 128–131,072 MiB and 1–100,000 files.
Preferences apply to future GUI/CLI admissions and retain existing content when
lowered. Working quotas are separate from checkpoint limits. No connector or
new dependency is needed.

Package drag-out and temporary script/map copies need no separate setup. A
cancellable worker verifies an entire temporary batch before handing off paths;
the studio owns successful copies for the session. Code copies remain read-only
until Save As creates an independent file outside the temporary-copy directory.
Package maps use Save As into a project or another permanent folder; their
temporary-copy storage cannot be selected as a map-save destination.
Use Extract Selected for batches over 2,000 files, 10,000 entries or 512 MiB,
or to review conflicting output names. File > Temporary Package Copies reviews
this window's reservations and configures the defaults of 2,048 MiB, 8,000 files,
40,000 entries and 64 batches. Apply affects later copies and preserves earlier
ones. `package copy-limits` inspects/proposes the same policy; add `--write` to
save it. Review Retained Copies lists managed sessions under the OS temporary
directory, including copies left after a crash. It shows actual payload usage
and offers explicit, checksum-reviewed discard of unused sessions; live windows
retain native ownership. `package copy-sessions` and `package copy-discard`
provide the same inspection and reviewed cleanup, with no writes by default.
Shared Storage Limits in the retained-copy review also caps initial reservations
across every window/process using that store: 8,192 MiB, 32,000 files, 160,000
entries and 256 batches by default. `package copy-store-limits` inspects/proposes
this policy; `--write` persists it in the temporary store independently of the
preferences file. Crashed and failed-cleanup reservations stay charged until
their session is removed. Lower limits preserve copies. Older or invalid session
records require review before new admission; later consumer growth remains
outside the initial reservation budget.
Old unregistered copy folders are outside this workflow. No setup step or cloud
service is required.

Archive/folder opening requires no setup and applies fixed entry, path-depth,
metadata and source-fingerprint limits in both GUI and CLI. The existing open
progress dialog reports directory indexing and index preparation with Cancel.
Editable-base metadata, WAD directory reconstruction and ordering continue on that
worker with record progress and Cancel. Limits are checked before growing the
editable base. An admission error asks for a smaller source and leaves the open
document and Undo/Redo intact. Subset preparation and save/reopen share this path;
cancelling adoption after publication leaves the saved output committed.
Package exports and no-write dry runs enforce the same disk-index policy, with
record progress and cancellation in the save dialog, including initial plan,
folder and WAD preparation. Cancel preserves the document and Undo/Redo and
requires no extra setup. Package folder creation, rename, delete, unstage and
Undo/Redo also use cancellable worker preparation, with record progress and the
existing accessibility/language preferences. Cancel leaves the current edit
history unchanged; there is no new setting. Package entry listing/filtering
also uses background preparation, with visible records checked and Cancel/Retry
next to the filter. Changing folder or query replaces pending work; entry actions
become available with the complete current result. The list inherits theme, text
scale and language preferences and adds no setup control. Folder navigation and
composition share that background index. The native folder tree inherits the
same preferences and retains selection during query changes; folder Rename/Delete
can proceed while its entry list is filtering. Oversized output is refused
before publication, preserving the document and existing destination/backup.
There is no additional setup control; see
[Export and reopening limits](PACKAGE_MANAGER.md#export-and-reopening-limits).
Combined sessions and multi-folder map texture lookup share those budgets across
at most 64 layers/roots; they require no additional preference or setup step.
Retained document content also has fixed defaults: 256 MiB of generated bytes
and 128 MiB of payload hashes across base and history. Refusal preserves edits
and offers a smaller-input/draft workflow, without adding a first-run setting.
Retained metadata has fixed 1,000,000-record and 128 MiB index/text limits;
operations and history share them. Undo/redo remain usable at the cap, and
metadata refusal preserves the current document. Reopening an exported package
releases history; saving a draft preserves it. Reader views also have fixed
record/text/depth limits. **Package view unavailable** explains a refused view
while preserving document history; Undo can restore it. There is no new setup
control. See [Reader snapshot limits](PACKAGE_MANAGER.md#reader-snapshot-limits).
Drafts and recovery retain protected source paths after reopening or Save As.
Their collection uses fixed record/text limits and the existing progress/Cancel
controls; no setup choice is needed. Export, extraction and temporary copies show
records checked while matching source protections and return to byte progress
when reading content. Progress labels use contrasting text over both the fill and
empty track, and their height follows text-size changes during an operation.
Compact binary quantities and exact bytes in accessible text/tooltips follow the
current locale. Partial progress stays distinct from completion; unknown totals
stay indeterminate, or static with reduced motion. These use existing theme,
text-scale and accessibility settings without another setup choice.
Older drafts are upgraded on their next save.
A protected output or save-lock collision asks for another destination.
Internal plan construction has separate fixed row/text/key limits and needs
no setup: 250,000 live entries/conflicts, 500,000 path/parent keys and 128 MiB
of text per representation. Refusals preserve history; folder edits check
expanded metadata before adoption. Individual edits and grouped imports also
check the final browser view before committing history. Refused or cancelled
group admission preserves the original document; legacy history remains
recoverable with Undo/Redo. See
[Plan preparation limits](PACKAGE_MANAGER.md#plan-preparation-limits).
Persisted history-counter limits also need no setup. Refusal preserves Undo/Redo
and draft saving; export/reopen starts new history when further edits are blocked.
See [Opening limits](PACKAGE_MANAGER.md#opening-limits),
[Retained document content](PACKAGE_MANAGER.md#retained-document-content) and
[Retained document metadata](PACKAGE_MANAGER.md#retained-document-metadata).

Known unreadable package entries stay visible and can be explicitly replaced or
deleted. Drafts preserve their unavailable metadata; archive export stays blocked
while those entries are current. No setup change is required.
Editable packages retain their original content provider. Filesystem adapter
sources are captured when opened; missing or ambiguous entries stay unavailable
until explicitly replaced or deleted. A later file at the same path cannot silently
supply their bytes. These checks share existing progress/cancellation and require
no new first-run option.
Repaired packages can checkpoint an unreadable deleted/replaced original as
explicit unavailable history. Recovery status and review show the count;
Undo may expose that missing content and block export until repaired again.
This uses existing recovery preferences and requires no new setup option.

Package browsing, preview and extraction use staged content immediately; no
preview mode or additional setup is required. The package inspector prepares
text/audio/model metadata on a background worker and shows Cancel Preview / Retry
Preview above its tabs. Old text clears when selection changes. Undo/redo
refreshes the package and asset browsers. Repeated source names are labelled by occurrence in Packages.
Texture selection and thumbnail decoding run in the background; Cancel Preview
and Reload Previews are available without a connector or additional setup.
Package preview and detail text follow text-scale changes without reopening
the document. Detail-header actions move below the title when necessary; long
source paths remain available in full through tooltips and accessibility text.
Metadata headings scale with the text and scroll horizontally in narrow panes.
These changes use the existing theme, language and text-scale preferences. Oversized preview totals use exact bytes and existing locale
number formatting, with no new setting. A read failure retains the declared
size while explaining why content is unavailable. Ogg header timing is labelled
as an estimate; partial, inconsistent or oversized timing stays unknown. No codec
installation or setup change is required for metadata. Full audio import and
playback still depend on the supported codec and complete sound validation.
Replace, Rename and Delete
use the selected file occurrence.
Extraction Paths appears only when selected output names collide and lets the
user choose distinct relative paths before extracting. It uses the existing
theme, scaling and language settings, with no new setup or connector.

Numeric level rotation works offline without additional setup or an AI
connector. It inherits theme, scale and reduced-motion preferences. Texture
lock is enabled in each new rotation dialog; map-wide Valve 220 conversion is
an explicit, unchecked option and requires a compatible project compiler.
No permanent setting or compiler migration is applied by opening the dialog.

Code can create untitled text documents without a project, game installation,
or AI connector. Save As chooses their location and file type. Local text
recovery is enabled by default; **File → Recover Text Documents** controls it.
Copies may contain private edits and stay in `code-recovery` under local app
data or beside the selected settings file. Turning recovery off keeps existing
copies. See [Code Editor](CODE_EDITOR.md) for restoration and CLI export.

Go to File discovers project source/media files and open-package metadata locally
in the background. It requires no connector or additional setup; progress,
cancellation and refresh remain available while the picker is open. The shared
project listing is also available through `project files` on the CLI.

The Code Files panel scans project filenames and metadata in a local background
catalog, with Cancel and Refresh Tree controls. It needs no external tool or AI
connector and shares listing/filtering behavior with `code files` on the CLI.

Parameter Hints (Ctrl+Shift+Space) needs no separate setup beyond a language
server that advertises call signatures. Provider trigger characters open it
while typing; the inline panel offers overload selection and expandable
documentation. It never changes the document and remains optional.

When a connected server advertises document diagnostics, VibeStudio requests them automatically
and offers **Refresh diagnostics**. Successful Save/Save As also notify an
interested provider after synchronizing saved text. No extra startup permission
or automatic connection is introduced.

Completion snippets need no additional package or preference. When a connected
provider offers one, acceptance selects its first field and the inline bar
exposes choices and navigation. Tab/Shift+Tab visit fields; Escape or Finish ends
the session. The inserted text stays in the normal unsaved document with Undo
and recovery. Regex transforms and stacked snippet sessions remain unsupported.

Code's optional **Language Server** panel can connect a user-installed local
stdio tool for formatting, reviewed symbol rename and code actions, Quick Info, semantic completion,
definitions, references and diagnostics. Its executable, literal
arguments, language and extensions are remembered, but every connection needs
an explicit **Connect**. Project changes disconnect. Matching named buffers,
including unsaved text, are shared with the selected executable; choose a trusted
tool. No server is installed or started by first-run setup. See
[Local Language Services](LANGUAGE_SERVICES.md).
Completion-capable providers may also advertise deferred resolution. Highlighted
suggestions then retrieve documentation and related imports through that same
explicit connection; no extra connector or setup is required.

Code definitions and completion also have a local background index; no language-server
installation or AI connector is required. Open a project or a saved source file,
then use **Index Code** to inspect its scope and warnings. Cancellation, refresh,
and symbol filtering are available in the Index output panel. Live snapshots
stay in memory; indexing never saves unsaved files or changes compiler inputs.

Quake III projects expose **Add Patch** and **Edit Patch Control Points** in
Levels without another tool installation or AI connector. Patch editing starts
with the map's current grid, offers known map material names, and uses normal
Save/recovery and package dependency inspection. The surface preview uses a UV
checker; it does not require game assets. See [Level Editor](LEVEL_EDITOR.md).

Quake-family maps also expose **Edit Brush Components** for vertex, edge and
face authoring. It inherits the current level grid, theme, scale and language;
the surface preview needs no game assets or AI service. Apply joins the map's
normal undo, saving, recovery and dependency workflows. No new setup preference
is required.

Map creation, saving and recovery require no game install or AI connector.
Local map recovery is enabled by default; **File → Recover Maps** controls it.
The recovery folder follows the selected settings profile, and backups sit
beside the saved map under `.vibestudio/map-backups`. Choose your own texture
names when creating a starter room and review Dependencies before compiling.
See [Level Editor](LEVEL_EDITOR.md).

Project search needs only an open project folder. **Code > Find in Project**
inherits language, theme, scale, and layout direction and works in AI-free mode.
Search options are local to the current workbench; no new first-run setting or
external tool is required. Named open Code documents override disk contents;
their reviewed replacements remain unsaved and undoable. Unopened files are
saved atomically. Changed snapshots and unsaved Levels maps block replacement.
See [Project Search](PROJECT_SEARCH.md).

Code files need no encoding preference at setup. UTF-8 and BOM-marked UTF-16
are detected per tab and retained on Save; the editor readout exposes the format.
Unsupported encodings open read-only. No AI connector or external tool is needed
for supported formats. See [Code Editor](CODE_EDITOR.md).

Texture authoring is available immediately through **Textures → Texture
Editor**, without a game installation or AI setup. It inherits the studio
theme, text scaling, and layout direction. Tab reaches both action toolbars;
named overflow buttons keep collapsed actions available at larger scales.
Save As supplies Ctrl+Shift+S if the platform theme has no standard binding.
Open a package or configure a game
installation to resolve a real palette; generated palette fallbacks are
labelled. Painting defaults to the replacing Pencil; Brush and shapes offer
explicit alpha modes, and wrapping/tolerance controls are available in Paint.
Transform provides canvas padding/cropping and resampling. Selection provides
resize/rotation of selected pixels with a center anchor by default. These
controls need no external tool or setup and inherit the studio's accessibility
preferences. Tool preferences last for the editor session. Save editable layers and
palette/export metadata in `.vtexture` projects. Export offers PNG, TGA, PCX,
Quake miptexture/WAD2, WAL, and Doom flat/patch profiles without extra tools.
Indexed game output needs an actual palette or explicit acceptance of generated
colors. Source-port limits are opt-in; preview mips and engine warnings before
writing into the project's asset roots. Stage Export uses the selected profile;
WAD2 accepts miptextures and Doom WADs accept flats/patches with native namespace
placement. Stage and Apply accepts PNG/TGA for Quake III, WAL for Quake-family
maps and matching miptexture lump names for WAD2. Keep file textures under the
game's `textures/` root; configure the saved Quake WAD in the map/compiler WAD
search path. Map and package saves remain separate. Refresh Palette Source reloads a changed staged palette in the
background without recoloring existing pixels; Cancel keeps the previous
palette. Browser thumbnails load visible rows within a shared memory budget;
dimension/format searches scan metadata in the background without additional
setup. Browser PNG export provides progress, cancellation before publication and
guarded replacement without extra setup. See [Texture Editor](TEXTURE_EDITOR.md).
Native project saves retain previous versions beside the destination under
`.vibestudio/texture-backups`; no additional setup or cloud service is needed.
Cancel Operation is available while preparing a project save. Final publication
runs separately, rechecks the destination and acknowledges the saved document
only on success; cancelling preparation leaves the editor and its edits open.
The Recovery inspector enables local checkpoints every 30 seconds, with a
5–600 second interval and an off switch. These preferences are available in the
editor, not yet the setup wizard (`textures/recoveryEnabled` and
`textures/recoveryIntervalSeconds`). Checkpoints contain layer pixels, palette
settings, and source-path metadata in the application data folder, or beside an
explicit settings-file profile. Restoring requires a new project destination.

Package review, integrity validation, and saving require no additional setup or
AI connector. Their dialogs inherit the configured language, UI scale, theme, and
layout direction. **Review Changes** checks the current staging plan; successful
GUI saves open their output with clean staging and update the recent-file
context. Cancellation and retry use the same local package services in every
setup profile. See [Package Manager](PACKAGE_MANAGER.md).

VibeStudio should include a modern first-run setup flow that lets users tailor
the overall application ecosystem to their needs before serious work begins.
The setup flow should be fast, skippable, resumable, accessible, and honest
about what has been detected automatically.

The setup flow is not a marketing tour. It is a practical configuration
workbench for accessibility, language, game installations, projects, editor
profiles, compilers, AI connectors, CLI use, and workflow preferences.

The Levels Health texture check uses the existing language, text scale, theme,
layout direction and reduced-motion preferences. Its background progress and
Cancel/Retry controls require no additional setup. Workspace package counts and
level texture availability follow the current staged package, including Undo
and Redo. An unavailable plan reports its blocking reason.

## Principles

- Start with accessibility and language so users can comfortably complete the
  rest of setup.
- Detect what can be detected, but never make destructive or private choices
  without confirmation.
- Keep every step skippable and resumable.
- Explain what was found, what was not found, and what can be configured later.
- Offer role-based presets without hiding advanced configuration.
- Save setup as editable preferences, not one-time decisions.
- Make AI opt-in and AI-free mode obvious.
- Provide a final review screen with tests, warnings, and next actions.

## Setup Steps

### 1. Welcome and Access

The step's **Open Accessibility Settings** button opens Settings >
Accessibility; the language, region, theme, scale, typeface, and spacing
choices sit on Settings > Appearance and Language beside it.

- [x] Choose language. The default, **System language**, follows the operating
  system's language whenever VibeStudio has it (47 languages; see
  [Supported Languages And Regions](ACCESSIBILITY_LOCALIZATION.md#supported-languages-and-regions)),
  and a different choice offers **Restart Now**. After a restart into a
  right-to-left language the whole window mirrors, panels included: they open
  on the left, and a panel moved beside the mode rail stays beside it. The
  **Run Build Pipeline** and **Launch Game** buttons keep their leading spacing
  in either direction.
- [x] Choose region formats: the system's regional settings, the interface
  language's, or any regional locale, with a live sample.
- [x] Choose UI scale and font size: text scale 100% to 200%, and the interface
  typeface (any installed family) with optional WCAG 1.4.12 text spacing.
  The shell uses the native UI typeface with a 10.5-point body text baseline;
  the text scale applies to this baseline throughout setup and the workbench.
- [x] Choose theme: system, dark, light, high-contrast dark, high-contrast
  light.
  System follows the platform colour scheme with Qt 6.5 or later, and the
  platform's high-contrast mode with Qt 6.10 or later; earlier Qt versions use
  dark. Explicit light/dark and high-contrast choices work on both.
  Live theme changes retain open content, text selection and local widget styles.
  Reapplying the same style skips a full restyle; changed styles use bounded
  refresh delivery through nested controls. No additional setup is required.
- [x] Choose colour vision (standard, red-green safe, blue-yellow safe,
  monochrome), reduced saturation, a thick focus outline, and a thick text
  cursor.
- [x] Choose reduced motion and a steady text cursor.
- [x] Choose how long status messages stay, screen reader announcements, and
  taskbar alerts for finished work.
- [x] Enable or skip OS-backed TTS, and choose its events, voice, rate, pitch,
  and volume.
- [x] Test TTS output if enabled: **Say Test Phrase**, or
  `vibestudio --cli accessibility speak --test`.
- [ ] Show keyboard navigation help and allow keyboard-only completion.
  **Keyboard Shortcuts…** on the Accessibility page opens the key reference;
  the guided keyboard-only walkthrough is still planned.

### 2. Workflow Role

- [ ] Choose primary role presets: mapper, package maintainer, artist,
  programmer/scripter, shader author, audio creator, release maintainer,
  all-in-one, or custom.
- [ ] Choose experience level: guided, standard, expert.
- [ ] Choose default detail level for logs, metadata, and inspector panes.

### 3. Editor Familiarity

- [x] Choose among 24 level-editor profiles, including VibeStudio Default,
  GtkRadiant 1.4/1.5/1.6, QeRadiant/Q3Radiant, NetRadiant Custom, TrenchBroom,
  QuArK and Hammer, or decide later. Profiles adapt layout, 3D camera, mouse
  gestures, grid and keys through shared studio services. See
  [`EDITOR_PROFILES.md`](EDITOR_PROFILES.md) for the explicit coverage and gaps.
- [x] Preview key differences: layout, camera, grid, selection, clipping,
  shortcuts, and terminology.
- [ ] Import or skip shortcut/profile files where supported later.

### 4. Game Installations

- [x] Auto-detect Steam and GOG install candidates.
- [x] Let users skip game installation setup and resume it later.
- [ ] Detect source ports where possible.
- [x] Let users add installs manually.
- [x] Validate expected packages/executables.
- [x] Let users import detected candidates only after confirmation.
- [x] Let users mark installs as read-only, active, hidden, or later in stored
  profile data.

### 5. Projects and Packages

- [ ] Open existing project.
- [ ] Create project from folder.
- [ ] Create empty project.
- [ ] Mount folders, WADs, PAKs, PK3s, and package roots.
- [ ] Choose output, temp, backup, and export directories.
- [x] Explain and persist project-local settings overrides in the manifest.

### 6. Toolchains and Build

- [ ] Detect bundled and system compiler tools.
- [ ] Configure ericw-tools, q3map2, ZDBSP, ZokumBSP, and source-port launchers.
- [ ] Choose default compiler profiles by engine family.
- [ ] Run a harmless toolchain probe and show results.
- [ ] Enable command manifests and output-path reporting.

Until this step lands in the guided flow, compiler executables are located on
the Build page's **Toolchain** tab, which saves the same user override as
`vibestudio --cli compiler set-path` and stays editable afterwards. The compiler
status chip opens that tab.

After opening a Quake-family map and its asset package/draft, **Build > Prepare
Build Workspace** can capture current edits and staged assets into a new
directory. **Use in Build** pins that snapshot; later edits require preparation
again. After a successful run, **Build > Publish Prepared Build** reviews the
captured assets, BSP and target-specific lighting/shaders, then writes a PAK or
PK3. **Details** expands the complete review and retained build warnings while
the initial view keeps the package file list visible. Choose a destination outside the workspace/source assets; source-map
inclusion and replacement with a verified backup are explicit options. It works
offline through the normal package services. **Build > Open Prepared Assets**
opens the full snapshot for general inspection. **Deploy Prepared Build** reviews
the matching Quake-family installation, game folder, full PAK/PK3 and optional windowed
launch. Read-only profiles require permission for that deployment only; replacing
a package keeps a verified backup. **Build and Launch** opens this review after
compilation. Quake/Quake II use the target selector and ericw-tools, with PAK
publication. Direct WAD2 texture drafts are accepted for Quake. Classic deployment
adds a PAK slot control: Automatic reuses this map's remembered slot or chooses
a free loadable slot; explicit selection requires another review. Quake needs a
consecutive PAK sequence, while Quake II supports slots 0–9 with gaps. Repeated
tests replace the same reviewed package with a backup. The single-map shortcut
stays disabled for prepared inputs. The
guided first-run flow still needs to expose this choice.

### 7. AI and Automation

- [x] Choose AI-free mode, configure later, or configure now.
- [x] Configure provider connectors: OpenAI, Claude, Gemini, ElevenLabs, Meshy,
  local/offline, or custom connector.
- [x] Choose preferred provider per capability when configured.
- [ ] Explain what project context may be sent to providers.
- [ ] Configure consent, redaction, cost/usage display, and generated-asset
  provenance.
- [x] Set the image model: Settings > AI and Automation > Image Model takes
  each image connector's model and endpoint (OpenAI's Images API, Gemini, a
  local Stable Diffusion web UI at `http://127.0.0.1:7860`, or a custom
  endpoint) and says which connector the Texture Generator will use and what
  stops it. The CLI sets the same with `--set-ai-image <connector>`,
  `--set-ai-image-model <connector>=<model>`, and
  `--set-ai-image-endpoint <connector>=<url>`.
- [x] Set the sound model: choose ElevenLabs (or a custom endpoint) as the
  Audio connector, then Settings > AI and Automation > Sound Model takes its
  model (`eleven_text_to_sound_v2` by default) and endpoint and says what
  stops it. The Sound Generator's synthesizer needs none of this. The CLI
  sets the same with `--set-ai-audio <connector>`,
  `--set-ai-audio-model <connector>=<model>`, and
  `--set-ai-audio-endpoint <connector>=<url>`.
- [x] Enable or skip agentic workflows.

### 8. CLI and Integration

- [ ] Offer CLI path setup instructions.
- [ ] Show current CLI executable path.
- [ ] Enable shell integration instructions where appropriate.
- [ ] Configure default JSON/text output preference for copied commands.
- [ ] Offer file association and shell-open integration where supported.

### 9. Review and Finish

- [ ] Show summary of accessibility, language, theme, editor profile, installs,
  projects, compilers, AI mode, CLI, and warnings.
- [ ] Run optional smoke checks.
- [ ] Save settings.
- [ ] Offer export of setup profile.
- [x] Open workspace dashboard with detected next actions.

## Setup Profiles

Setup should produce an editable profile that can be exported, imported, or
project-overridden.

Profile areas:

- Accessibility and language.
- Theme, scale, density, motion, and TTS.
- Editor profile and shortcuts, with keys of the user's own for any command
  (Help > Keyboard Shortcuts), kept apart from the profile.
- Startup and recovery: whether the last session reopens at start (on by
  default: the package, the map, and the code files that were open, on the page
  the studio closed on), and whether a crash leaves a report on this machine
  for the next start to offer (on by default; reports are never sent).
- Game installations and source ports.
- Project defaults and package mount preferences.
- Compiler and launch profiles.
- AI connector configuration and AI-free mode.
- CLI and automation defaults.

## MVP Acceptance

- [x] First-run setup appears on a clean profile and can be skipped.
- [x] Accessibility/language choices are available before visual-heavy setup.
- [x] High-contrast and scaling choices apply immediately through the shared
  preferences path.
- [x] Game installation setup supports auto-detect, manual add, skip, and later.
- [x] Editor profile selection is present with routed MVP presets.
- [x] AI-free mode is visible and selectable.
- [x] OpenAI-first AI connector settings are represented without blocking
  non-OpenAI connector design.
- [x] Setup summary explains incomplete, skipped, failed, successful, warning,
  install, toolchain, AI, and CLI states.
- [x] Setup choices are editable in preferences after completion.

## Current Scaffold Slice

Packages needs no additional setup for source integrity checks. ZIP/PK3 filename
decoding automatically follows UTF-8, Unicode Path or CP437 metadata; no encoding
preference is required. Regional encodings without valid metadata have no manual
override yet. Opening archives
or folders and staging local files fingerprints their content on workers with
progress and cancellation. Cancelling before adoption keeps the prior package
and staged plan, including when worker completion is waiting to be delivered.
Installation palette lookup also reads on a worker; cancelling uses fallback
colors, including a cancellation before the prepared palette is adopted. These
checks work offline and require no AI connector.
Package extraction also runs on a worker with byte progress and within-file
cancellation. It requires only an output folder; no extra tool or preference
is needed. Completed files remain after cancellation, and the current partial
file is discarded. Extract actions use the current planned package. A completed
save or extraction still reports its committed output when a late cancellation
arrives; these controls require no new preference.
Package summaries and before/after composition also require no setup. The
Overview chart follows the selected text scale and inspector width, with full
category values in its tooltip and accessible summary. Opening
prepares their metadata on the existing worker. Oversized totals have a textual
diagnosis instead of a wrapped byte count; an oversized output blocks export
while retained history supports recovery. Entries remain browsable so an
offending entry can be deleted and a repaired package exported with Save As;
Undo/Redo restores the corresponding size warning and export state. Missing
plan statistics are explicitly unavailable, and the CLI exposes exact aggregate
sizes as decimal strings. Save As includes a staging manifest by default; its
arrow menu lets users clear **Include Staging Manifest** when repairing damaged
original payloads that cannot be hashed. The choice lasts for the current
window, uses the existing CLI export service and needs no first-run preference.
The Staging tab's native list and resizable change-details pane require no extra
setup. Select an operation or diagnostic for complete selectable text; existing
Enter, Unstage, Delete/Backspace and multi-selection behavior remains available.

Models > Mesh Editor also works without extra setup or AI. It opens the selected
OBJ/MDL/MD2/MD3 model as an authoring copy, imports a source file, or starts with a cube.
Design Prop > Edit as Mesh bakes primitive geometry explicitly. Save the editable
`.mesh.json` separately from MDL/MD2/MD3/OBJ outputs and the package/map plan. OBJ
polygon import needs no connector or extra library. Export without Wavefront
material libraries in the source modeller and assign package materials here;
direct material paths, UVs, normal seams and smoothing groups are retained.
OBJ keeps the supplied units and axes. Package OBJ and native model previews load
on a cancellable worker using the current staged view. Cancel Preview stops loading;
select the model again to retry. Native metadata and per-surface material details
remain available after decoding. See [OBJ interchange](MODEL_MESH.md#obj-polygon-interchange).
Filled and wireframe views prepare in the background with visible rendering
state. Navigation stays available; component picking resumes when the camera
and pose are ready. The preview uses the existing CPU renderer and needs no
graphics backend, connector or setup preference change. Wireframe retains every
edge and uses thicker dashed selection cues; display scaling and high-visibility
themes adjust stroke widths automatically.
Vertex markers and their picking index also prepare in the background. Dense
component selections and frame changes preserve table context, and unchanged
inspector refreshes reuse the completed preview. These optimizations use the
same local CPU renderer and require no additional setup.
Browser **Export OBJ** also runs with progress and cancellation. Choose its
output outside the source package folder or draft; those files stay under the
package editor's staging workflow. No new setup option or dependency is needed.
Mesh
authoring includes face/vertex/edge selection, conforming edge splits, and
distance welding. Welding preserves UV and normal seams by default; its distance
and seam controls are local to the current editor and require no setup preference.
Precise vertex picking, optional X-ray selection, move/rotate/scale gizmos, and exact
orthographic/perspective view presets are available immediately. Snap starts off;
the initial steps are 1 model unit, 15 degrees, and 0.1 scale factor. The tool,
steps, and pivot settings are local to the editor. Geometry defaults to the
selection bounds centre in the displayed pose, with origin and custom alternatives.
Numeric transforms share these settings and need no mouse gesture or extra setup.
Transform axes starts at World. Selection follows a selected face, tag or box;
Custom reveals XYZ orientation angles. The displayed pose fixes axes for all
affected frames. These are editor session controls, requiring no setup preference
or source migration; the resulting geometry persists through save and recovery.
Rotate includes a **Free** centre handle and dashed trackball circle immediately.
Drag the centre or unoccupied circle interior; axis rings remain constrained.
With Snap enabled, free rotation keeps its axis and rounds its total angle.
Numeric XYZ controls and the CLI can reproduce its preview with angle snapping
off. It shares the existing frame scope, pivot, undo and recovery settings and
needs no setup preference, dependency or source migration.
Repair Import needs no setup or dependency. It prepares supported damaged mesh
and native sources for review, then saves and opens a new `.mesh.json`. The
original source and existing destinations remain protected. Its preview, report
and destination are local to that import; normal authoring and recovery resume
from the saved repaired copy.
The Health inspector needs no installation or saved preference. Inspect Surface
reports indexed findings; Select Findings highlights them in the existing table
and previews. Explicit repairs cover the entire active surface and every pose,
with one undo step each. Open boundaries are informational and are not filled
automatically. Review deliberately two-sided faces before removing duplicates.
The Nonmanifold Edges category also offers Split Nonmanifold Edges. It copies
affected endpoints while preserving existing two-face connections, every face
and all animation samples. Review the resulting boundaries before further
welding or export. This action needs no additional setup or saved preference.

Health also provides **Geometry intersections > Inspect Intersections** across
all surfaces, with All poses or Current pose scope. **Show First Face** and
**Show Second Face** select the reported face at its affected pose for ordinary
authoring. Inspection runs with progress and Cancel and changes no source or
history. It needs no additional setup, compiler, provider or saved preference;
the same scan is available through `model intersections`.
To close a chosen hole, select one of its boundary edges in Edges mode and use
Geometry > Fill Boundary Loops. The displayed pose chooses the cap; every pose is
checked before the edit commits. New faces stay selected for UV/normal finishing.
This offline operation needs no library installation, connector or new preference.
See [boundary filling limits](MODEL_MESH.md) before repairing heavily folded or
animated holes.
To connect two open boundaries, select an edge from each loop on one surface and
use Geometry > Bridge Boundary Loops. Unequal vertex counts are supported;
Bridge twist adjusts the second anchor. Join separate surfaces first if needed.
The displayed pose chooses alignment and every stored pose is validated before
one undo step commits. New faces remain selected for UV/normal finishing. This
uses the same offline setup and progress/Cancel controls as boundary filling.
Surface > Manage Surfaces needs no setup or extra preference. Review rename,
separate, move, duplicate, delete or join operations, then apply one all-pose
edit. Separate and Move use the selected faces; Join uses its own checked surface
list, initialized from the persistent Surfaces-mode selection. Material
replacement starts off and must be explicitly chosen when binding
lists differ. See [Surface Authoring](MODEL_SURFACES.md) for native-name constraints
and effects on external `.skin` references.
Choose Surfaces in the selection control to move, rotate or scale multiple whole
surfaces around one common pivot. Table and viewport selection, numeric edits,
undo and recovery share that set without a setup preference. Materials and UV
inspection use its active member; component editing requires the corresponding
selection mode. Tags and collision remain separate. See the surface guide above.
Surface > Manage Material Slots needs no installation or preference. Review
ordered external paths and apply once; Preview slot is a session-only choice
per surface. Assign Material edits the chosen slot; export keeps authored order.
Embedded MDL skins continue to use Animation's native controls. Material previews
read the current package/staging snapshot, so open the project's assets first.
See [Material Slots](MODEL_MATERIAL_SLOTS.md) for native constraints and CLI use.
Animation clips need no additional setup. Choose a saved clip or All frames;
Preview FPS is a session control (0.001–1,000). Selecting a clip uses its saved
rate when present. Saved clip FPS / Apply Clip FPS retains fractional timing in
mesh schema 6; zero means unspecified. Game timing is configured separately.
Smooth preview starts enabled in the mesh editor and blends the selected clip,
including its end-to-start loop. Disable it for discrete stored frames. Pause
returns to the stored pose for editing; neither setting changes the source.
Reduced motion still disables automatic playback. Add, rename, range and delete
controls edit source metadata. Copy Full Pose uses the displayed destination
frame; Insert In-between Frames uses it and the next frame across all surfaces
and attachments. Generated names default to the `blend` prefix. These edits use
normal validation, undo and recovery; native exports report omitted clip metadata.
The UV tab adds island selection, framing, pan/zoom, and a move handle. Surface
provides seam marking, detachment, numeric UV pivots, and optional offset snapping.
The UV worker renders dense selections with the existing CPU/Qt backend and
keeps selected edges and points above unselected components. It needs no graphics
backend setting, additional installation, AI connector or first-run preference.
The UV pivot starts at the origin; snapping starts off with a 0.125-tile step.
Individual Islands rotates/scales each complete chart around its own centre.
Use Pick Islands or Select Islands first; shared corners split while every pose
keeps its geometry and normals. Projection uses each chart's projected bounds
in the displayed pose. This pivot mode needs no additional setup or preference.
Unwrap and Pack Faces, Pack Selected UVs and Pack Around Unselected need no extra
installation or AI. For existing painted regions, select complete moving islands
and use Pack Around Unselected. Fixed-region packing offers uniform fit or
unchanged UV scale. Other surfaces sharing a material slot are also protected;
fixed UVs must lie within the 0–1 tile. These are operation choices, not first-run
preferences.
Select faces, then choose atlas width/height (default 512 square pixels) and
padding (default 4 pixels) in Surface. Turn off Same as width for a rectangular
texture, using the intended material image's dimensions. These are operation controls, not first-run
preferences. Mapping edits preserve all animation geometry and use normal undo;
existing texture images are not resized or repainted, and MD2 skin export
dimensions stay independent.
These controls are local to the editor and need no setup preference. Seam marks
are stored in version-3 editable sources and recovery; versions 1 and 2 still
open. Handoff provides named MD2 skin-size controls with an explicit Apply button.
New meshes and older schemas default to 256 by 256; imported MD2 sizes persist.
Assign a matching PCX material for MD2 export and choose a `.md2` package path
for staging. These document settings require no first-run preference. Native exports retain resolved UV corners, with marks kept in the source.
Surface's Apply .skin File and Apply Package .skin commands need no extra setup.
They copy complete Quake III surface-to-shader assignments into the model in one
undo step. Package import uses the existing staged snapshot and reports unused
bindings in Last Skin Import Details. Save/export/stage the modified model to
use its primary shader paths elsewhere; this does not establish a live link to
the skin file or change first-run preferences. See
[skin assignments](MODEL_MESH.md#quake-iii-skin-assignments).

The ordinary Models browser also previews alternate appearances in **Skin**:
choose a surface/material slot, an exact package `.skin`, or an MDL skin/member.
No new setup preference is required. These session choices retain the camera
and geometry frame, use the package palette and staged package snapshot, and
reset when the source context changes. Cancel stops loading; Reset
restores primary bindings. Opening Mesh Editor or exporting a browser frame
keeps original bindings, so use the editor to author a saved change. MDL members
stay fixed here; native timed skin playback remains in the Mesh Editor. See
[browser appearances](MODEL_MATERIAL_SLOTS.md#model-browser-appearances).

Quake MDL adds indexed skin slots/members, native pose groups, hold times and
header settings in its own inspector tab. The first indexed skin prepares a new
mesh; existing native MDL imports retain their groups and raw indices. Import
Package Texture reads exact entries from the open package, including staged
Texture Editor output, and copies a matching indexed image into one undo step.
Choose add, replace or append; package changes remain staged. Load a
768-byte game palette to replace a generated preview palette without remapping
indices. Preview Member affects only the model/UV display. Native preview chooses
stored software-Quake or original-GLQuake timing for the selected native frame and
skin; Seek Time and entity phase are deterministic session controls. Reduced
motion permits seeks and prevents automatic play. Use Clip Timing restores the
independent Animation FPS and Smooth preview controls. Save schema-4 sources, Export MDL or
stage a `.mdl` package path. Automatic placement still requires MD3. These are
document/session controls, with no new first-run preference or external tool.
Material images load in the background from the open package and its staged edits, and
fall back to the checker. Package undo and palette changes refresh them. Cancel,
Reload Images, and Details expose loading and missing-image diagnostics. No extra
connector, renderer, or preference is needed. Animation
playback follows reduced-motion settings. The shared preview renders on a worker
and shows when it is updating; model or material changes retire older work.
This needs no GPU backend. Keep local recovery copies defaults on and checks
unsaved mesh changes every five seconds. Its checkbox persists under
`model/recoveryEnabled`; turning it off retains existing copies. Recover shows
the local folder and restores a verified copy as an unsaved draft. Import,
editing, saves, and exports run in the background with progress and cancellation
for longer operations; controls unlock when the operation finishes. No connector
or game installation is required. Closing the studio during mesh work cancels
and waits, then continues after unsaved changes are resolved; cancelling that
prompt keeps the studio open. See [Editable Meshes](MODEL_MESH.md)
for current operations, source preservation, and the remaining release gaps.

Models > Mesh Editor also authors attachment tags offline without additional setup.
Show Tags starts enabled; Tags component mode keeps origins visible and selects
one named attachment. Animation contains names, origins, orientation reset and
pose copying. Pose scope shares Geometry's all/current-frame choice. Tags use
the existing move/rotate snapping and pivot controls; scale is disabled.
Authoring controls inherit language, theme, text scale and RTL settings. Save
the mesh source to retain poses and use MD3 for native attachment export and
package handoff. MD2 refuses tags; OBJ frame export reports their omission.

Models > Design Prop needs no additional setup, installation, AI connector, or
external modeller. Save an editable `.model.json` design, export MD3/OBJ, or open
a folder/PAK/PK3/ZIP on Packages to stage the generated prop. Stage and Place also
requires an open Quake III map. Levels > Dependencies reviews the staged model
and its material dependencies before exporting an asset package. Save the map
and package separately, and make the model/material files available in the
external compiler's game directory before compiling; see
[Model Design](MODEL_DESIGN.md) for this handoff and current limits. The dialog
inherits language, theme, text scale, and layout direction from normal settings.

Models > Assemble also works offline with no extra installation. Choose model
files or current-package entries, attach children to named tags and save an
`.assembly.json` recipe. Bake Animation / Export Animation reviews start time,
frame count and sampling FPS, then creates a separate sampled mesh or MD2/MD3
within normal format limits. Editable sources retain clip FPS. The linked
originals retain tags, collision and native skin metadata; game timing and
texture packaging remain explicit follow-up work. File references are relative to that source; package
references require the intended package to be open again. Playback settings are
independent per part. Bake Pose to Mesh enters the existing mesh editor for
staging, placement and compiler handoff. Keep local recovery copies shares the
Mesh Editor preference and defaults on. Applied recipe edits, selection and time
are checkpointed in the local `assembly-recovery` folder; turning recovery off
retains existing copies. **Recoveries…** restores a verified unsaved draft that
must be saved to a different source path. Referenced models and package bytes
are not copied. See [Model Assemblies](MODEL_ASSEMBLY.md).
Select a part in the preview or parts list, use **Part** for full 3D rotation,
and **Surface** for material and UV transforms. The generated **UV checker**
preview works offline without texture files. **Handoff** holds package and level
options. Existing schema-1 designs open normally; new saves use schema 2.

Level dependency inspection needs an open map and an open asset package or folder.
After choosing those on Levels and Packages, use Levels > Dependencies; package
and dependency subset exports are available without a game installation, compiler,
AI connector, or additional setup preference. These dialogs inherit the current
language, theme, text scale, and layout direction. Choose an export destination
outside the asset source folder. **Export Selected** reviews exact occurrences
and any required WAD map/GL groups, namespace boundaries or texture name tables.
The same workflow supports saved drafts and newly created packages. Its export
keeps document history and requires confirmation before replacing a separate
output. Incomplete groups show a blocker; complete game-asset dependency closure
still requires review.

Audio editing needs no game install, compiler, cloud connector, or required setup.
Use Audio > Open Audio for local PCM/float WAV, digital DMX, MP3, native FLAC or
Ogg Vorbis, or open a package
and choose Edit Sound. The editor inherits language, theme, text scale, and layout
direction. Its named summaries and recovery/validation messages expose their
current text to assistive technology without additional setup.
Qt Multimedia is optional for auditioning; sample editing, undo/redo,
lossless `.vsaudio` project saves, WAV/DMX delivery, and package staging work without it.
A new audition uses the system default output. Missing or disconnected devices
leave editing available and report a retryable error; choose an output in system
sound settings, then try Play again. No microphone or recording permission is
needed. The seek controls describe their millisecond backend resolution.
The browser and editor share these playback rules. Browser previews and audition
preparation run in the background; Stop cancels pending audition, and selection
changes discard old previews. WAV audition preserves its source precision.
Export Audio and Stage Sound offer explicit Doom/Quake-family sound-effect presets;
conversion uses a separate delivery copy and needs no additional installation.
**Stage & Place in Level** needs an open Quake II/III map and folder/PAK/ZIP/PK3.
It reviews the game, speaker position, loop/trigger mode and target name before
converting and staging. Unmarked legacy maps require an explicit Quake II choice.
Save both the map and package afterwards; each has its own undo history. Stock
Quake/Doom and mod-specific speakers keep their existing package/entity workflow.
Sample-rate conversion is included in every build and needs no additional
installation; choose **Effects > Resample…** inside the editor.
**Analyze…** measures selection/channel levels without a playback device or
additional setup. It shares its read-only service with `asset audio-analyze`.
True peak and integrated loudness are included even without Qt Multimedia.
Mono/stereo loudness has defined roles. For larger channel counts, review the
source-order speaker layout in the analysis dialog; it is not inferred or saved
as a project preference. Skipping loudness keeps sample and true-peak analysis.
No recording device or cloud permission is requested by these measurements.
**Markers…** adds exact cue positions and a forward loop without extra setup;
markers travel with native saves/recovery. Export presets explain game-specific
retention, and **Select Loop** prepares the range for normal audition controls.
The editor enables local recovery by default; **Keep local recovery copies**
stores the `audio/recoveryEnabled` preference. Copies contain samples and source
paths in the application data folder (or beside an explicit `--settings-file`
profile); **Recoveries…** verifies, restores, and
explicitly discards reviewed copies. Storage is limited to 32 copies / 512 MiB;
full storage preserves existing copies and reports that checkpoints have stopped.
**Settings → Getting Started → Audio Recovery** provides the same checkpoint
preference and a separate **Offer copies at startup** choice, both initially on.
Checkpoint changes synchronize with open waveform and multitrack editors. Startup discovery checks
bounded directory metadata in a worker, leaves an earlier crash notice visible,
and offers review without opening audio or taking focus. Disabling either option
retains existing files. The setup panel shows their folder and **Review Copies**;
**File → Recover Audio** and command-palette recovery are always available.
The `VIBESTUDIO_AUDIO_RECOVERY_ROOT` override takes precedence over profile storage.
See [Audio Editor](AUDIO_EDITOR.md).

**Audio → Multitrack…** needs no additional setup or cloud service. The session
inherits language, scaling, theme and reduced-motion settings. **To Session**
imports a waveform snapshot; **Edit Mixdown** returns a selected mix range to
the same analysis, delivery, staging and level tools. Sessions use mono/stereo
sources and explicitly review rate conversion. Optional Qt Multimedia is needed
only for audition. In the session window, choose **Output** and **Output buffer**
before Play. The initial selection is the system default with a 2,048-frame
requested buffer. These choices last for this window; they do not change OS
settings. Refresh discovers outputs; unsupported session rates fail explicitly.
No input device is opened. Save arrangements as `.vssession`; the Audio Recovery
checkpoint/startup preferences cover waveform and session documents together.
The shared manager labels their kind and restores an unsaved draft into the
matching editor. Session copies embed media and count against the same storage
limit. Recording and input-device preferences remain pending in the
[DAW plan](plans/audio-daw.md).

The current shell includes a persistent first-run setup panel backed by
`QSettings`. It tracks not-started, in-progress, skipped, and completed states;
stores the current setup step; exposes skip, resume, next, finish, and reset
actions; and shows its summary as a stepper: every step once, in order, marked
done with a check, current with an accent chevron, or pending with a dot and
muted text, then any setup warnings with a warning glyph. The list is sized to
show every step and warning without an inner scroll bar. Each step row is announced as, for
example, "Step 2 of 8, current step: Workspace Profile". The list used to show
the current step twice, once as "Current" and again among the pending items.
The same state is available from the CLI through `--setup-report`,
`--setup-start`, `--setup-step`, `--setup-next`, `--setup-skip`,
`--setup-complete`, and `--setup-reset`.

The full guided setup flow remains planned. Steam/GOG game installation
detection, manual install profiles, editor profile selection, language/theme/
scale/density/reduced-motion/TTS preferences, the full accessibility page,
AI connector preference storage, CLI setup reports, localization target
metadata, and setup summaries are active slices. The GUI loads compiled
translation catalogs at startup; a changed language takes effect after the
restart Settings offers. Portable release packages require
the compiled application catalogs and deploy Qt's own standard-dialog catalogs
through the platform runtime step; see [Packaging](PACKAGING.md). No extra
language download or setup preference is introduced. Most target-language messages
remain untranslated; see [Accessibility and Localization](ACCESSIBILITY_LOCALIZATION.md).
Source-port detection, deeper project/package mounting, and richer toolchain probes remain
represented as setup steps, preferences, release smoke checks, or warnings
until their dedicated roadmap slices land.

WAD Groups requires no game installation, compiler or AI connector. Open a Doom
WAD or its draft, review a group edit and apply it to the existing staging/history
workflow. New source-free WADs support the same review immediately and retain
their planned order through saving and draft history. WAD2/WAD3 retain individual texture editing. Group edits do not rewrite
map metadata, scripts or game-asset references; inspect those dependencies before
publishing. See [Edit WAD Groups](PACKAGE_MANAGER.md#edit-wad-groups).

Doom node readiness adds no setup preference or dependency. The existing ZDBSP
or ZokumBSP compiler configuration supplies rebuilt WADs. Geometry saves clear
obsolete node data; opening an authored WAD can therefore show a rebuild warning
until a node-built output is opened. Launch preparation checks that output on a
cancellable worker and uses the existing scale, theme, language and reduced-motion
settings. See [node readiness](LEVEL_EDITOR.md#doom-node-readiness).

## Restoring a portable workspace

After setup, File → Open Workspace restores `.vibeworkspace` references. Its
project selects the existing manifest and shared installation/compiler/palette
context. It does not rerun setup, enable AI, change language/theme, install
dependencies or execute a workspace-supplied command. Missing references are
reported so the workspace can be relocated. File → Save Workspace As captures
saved references; native editor documents still need their own saves. See
[Workspaces](WORKSPACES.md).


Multitrack **Export Stems…** needs only an existing destination folder; it adds
no device, plugin, AI or setup dependency. Choose tracks/buses, an optional master,
shared range and precision, and review the filename plan. Integer delivery offers
TPDF dither and a repeatable seed. Existing outputs require the explicit replace
control. Completed files remain after an interrupted batch; inspect **Delivery
Report…** or its JSON manifest before retrying. Stems share source protection,
project/session context and normal WAV handoff into waveform, package and level
workflows; editable project interchange remains future work.

The Mesh Editor's Collision tab supports optional per-frame box tracks without
new setup or dependencies. **Animate Box** copies a static volume into every
pose; **Fit New Animated Box** fits each pose independently. Current/all-frame
scope is shared with Geometry. Native model exports omit these volumes; map
handoff samples the displayed stored frame as static brushes. Animated sources
use mesh schema 7, with ordinary undo and recovery. See
[Model Collision](MODEL_COLLISION.md) for limits and CLI equivalents.

## Quake III Native Animation

Quake III native animation authoring requires no connector or additional setup. Open the relevant model assembly and use **Native Animation…** to bind lower/upper parts and import or author `animation.cfg`; package model parts use the current package context. The existing recovery and reduced-motion preferences apply. See [Native Animation](MODEL_ASSEMBLY.md#quake-iii-native-animation).

Linked `.skin` files also require no additional setup. In an assembly part's
**Materials** control, choose a skin file or open the intended package for a
package skin. Save the model and skin inputs alongside the recipe, and reopen
its package context when needed. Existing recovery and accessibility preferences
apply. See [Linked Skins](MODEL_ASSEMBLY.md#linked-skins).

## Native Player Package Setup

No connector or extra tool is required for **Model Assembly > Player Package…**.
Open the project's material package/folder, configure native lower/upper animation,
attach a single-pose head, and choose an icon. Review the generated file list and
native limits before exporting the PK3 outside the source asset folder. Package
Manager inspection/deployment uses the ordinary package workflows; publication
does not install assets or start a game. See [player packages](MODEL_ASSEMBLY.md#native-player-packages).

## Placed Model Appearance Inputs

Open the project's asset folder, archive or portable package draft before
reviewing MD3 `misc_model` appearances in Levels. The entity inspector's `_skin`
value selects a compiler suffix; Camera Details explains the derived filename,
material mappings, omitted surfaces and hashes. Native player skin previews use
a different surface-binding contract. Missing compiler inputs remain visible
as unavailable instances and block dependency export. To place an animated pose,
bake it as a static MD3 through Assemblies; the pinned compiler reads frame zero.
No cloud service, new preference or first-run step is required. See
[Placed Model Appearances](LEVEL_MODEL_APPEARANCE.md).
