# Accessibility And Localization

**Help** > **Documentation** uses the shared command registry, with a
translatable menu label and status tip. Standard Qt menu and Command Palette
accessibility exposes its name and keyboard access. Map-edit status messages
remain translatable, including plural forms, and describe Save Map's change
check and backup before updating the file.

Q3Radiant's position steering exposes its active state in the camera HUD and
accessible description, with an accessibility notification on start/end. Release,
Escape, focus loss, hiding/disabling and workspace replacement end motion.
Fixed movement and pitch keys remain an alternative to pointer steering.
The profile, Controls reference and four new gesture preferences use normal Qt
translation contexts and native named controls. Offscreen semantic checks cover
motion, cancellation, shared document preservation and 100%/200% high-contrast
RTL layouts; native keyboard and assistive-technology acceptance remains open.

Duplex recording storage/controller diagnostics use the `AudioRecording`
context, and read-only CLI inspection uses `AudioRecordingCli`. Worker states
distinguish permission, playback acknowledgement, preparation, recording, drain,
saving and terminal outcomes, with numerical per-arm stored frames and timing.
`AudioRecordingDialog`, `AudioRecordingMeters` and `AudioRecordingImport` cover
the native controls and shared validation. Record/Review tabs use scrolling forms, named native
checkbox trees, explicit per-take fields, wrapped labels and a fixed status,
progress, Stop/Cancel and Close footer. Buttons are not automatic Enter defaults.
Busy operations disable editing; the Meters tab remains available with named
native channel rows, peak/RMS bars, numerical maxima/headroom, over-range counts
and state text. Selected-row details wrap, numeric signs retain left-to-right
order, and reset exposes a pending acknowledgement state without changing audio.
The meter table scrolls independently for large text and expanded translations.
Close waits for cancellation/finalization.
Numerical stored-frame counts and timing accompany state text. Offscreen tests
check accessible names, focus eligibility, high contrast, 100–200% text scaling
and expanded RTL text. Native keyboard/screen-reader acceptance remains pending;
no physical input or OS capture is used by these tests.

Recording loop count and reviewed pass choices use named, keyboard-focusable
native spin boxes. The take list reports complete passes and partial-pass frames;
recording status reports captured versus requested passes. Per-pass trims use
the existing frame fields and retain original timeline alignment. All new text
uses the recording dialog/import translation contexts and the same scrolling,
high-contrast, scaling and RTL layout checks.

Recording comp review adds a named native section table, Add/Update/Remove
buttons and a focusable frame spin box. The queue reports track, pass, range,
placement and fade lengths; wrapped validation text explains invalid handles
or overlapping sections. Import uses a textual readiness state and tooltip,
and queue fields enter the comp only through explicit Add/Update. Controls use
Qt translation contexts and the existing scrollable high-contrast/expanded-RTL
layout. Native keyboard and assistive-technology acceptance remains open.

Recording review audition uses named native output/buffer, volume, repeat,
backing, frame position and Pause/Resume controls. Status text reports verification,
output preparation, playback state, consumed frame position, underruns and
clipping counts. The scrolling Audition tab shares the fixed Stop/Cancel footer.
Busy handoffs and verification disable editing; cancelling or closing cannot
start a late audition. Review changes stop stale playback. Frame values remain
left-to-right within RTL forms, and new text uses the recording dialog/CLI
translation contexts. No waveform-only gesture is required to seek or compare.

Saved recording reviews add named native Open/Save/Save As buttons and a
keyboard-focusable queue-mode checkbox. A wrapped file/status label distinguishes
saved and unsaved choices without relying on color. A native Save/Discard/Cancel
dialog protects changed choices before close or replacement, with Cancel as the
default. Save is available when the review is valid and idle. Open/save verification has
visible progress and uses the fixed Stop/Cancel control; failed or cancelled
loads retain the prior queue. `AudioRecordingReview` supplies shared file and
validation messages, with dialog and CLI text in their existing contexts.

Session **Meters…** uses named native controls and a keyboard-navigable table
for tracks, buses and master. Native-style level bars retain numerical dBFS
text and accessible cell values; over-range counts and signed correlation do
not depend on color. The signal selector and readings live in a scrolling
body; analysis/reset/cancel and Close remain outside it. The table also scrolls
for long names, large text and translations. Numeric text sits above thin level
bars so native themes do not place bar borders through the numbers.
The existing named session Loop checkbox retains its normal focus/navigation.
Loop meter counts cover every pass; the displayed playhead stays inside the
range. Repeating the same loop setting does not reopen output or clear levels.
Analysis uses the session worker's progress/cancellation and reduced-motion
behavior. Strings use `AudioMeters`, `AudioMeterDialog`, `AudioSessionDialog`
and `AudioSessionCli`. QWidget render fixtures cover dark/high-visibility and
200% expanded RTL; native screen-reader and physical-device acceptance remain open.

Session **Media…** uses native named source rows, operation/name/path controls,
multiselection for unused removal, and explicit textual file availability.
Review status, validation and cancellation remain visible; reduced-motion mode
uses a static progress indicator. Current/proposed waveform tabs reuse the
existing accessible waveform widget. All fields live in a wrapping scroll form
with Apply/Cancel outside it, and changing fields invalidates Apply until a new
review finishes. Source paths/digests are inspectable without relying on color.
Strings use `AudioMedia`, `AudioMediaDialog`, `AudioSessionDialog` and
`AudioSessionCli`. Scaled/expanded-RTL fixtures do not establish native
screen-reader or physical keyboard acceptance.

Camera **Place at Camera Surface** exposes a named, focusable Create action and
native numeric **Clearance** field in a wrapping form. The command palette
provides the same action without a mouse gesture. Placement/refusal status uses
text, and longer insertions reuse the cancellable placement worker and its
reduced-motion progress. Names, descriptions and status messages are
translatable. The UI fixture covers 200% high-contrast RTL and expanded strings;
native screen-reader and physical keyboard acceptance remain open.

Camera **Draw Brush** has named, focusable native plane/base/depth controls,
work-zone reset and a numeric primitive route. Dashed camera/plan outlines and
localized dimensions distinguish the draft from committed geometry without
colour alone. Start/end changes announce an updated camera description; all
plan panes describe their transient draft. The wrapping construction form and
HUD have 200% high-contrast RTL/expanded-string checks. Native input and screen
reader acceptance remain open. See [camera brush creation](LEVEL_EDITOR.md#camera-brush-creation).
Brush insertion reuses the named placement progress and focusable Cancel
controls, with geometry/insertion/history phase text and reduced-motion
behavior. Numeric Apply retains a stale draft and reports why publication was
refused. Progress is covered at 100% dark and 200% high-contrast RTL with expanded
translations; native assistive-technology acceptance remains open.

The Levels Objects list retains native Qt list/selection accessibility and
focus behavior with a model-backed data source. Two elided lines keep row
layout uniform as text scales; full multiline source fields and hidden state
remain available through tooltips and accessible text. The Objects tab labels
pending background filtering. The filter stays enabled for editing, clearing
and cancellation; Enter can select the current result when it becomes ready.
Query changes and source replacement cancel a queued selection. New status
strings and moved row labels retain the shell translation context. Semantic
widget tests cover large lists, high contrast and expanded RTL; native keyboard
and screen-reader acceptance remain open.
Brush material suggestions use the shared sorted names, omitting blank values
and Doom's no-texture marker. Workbench updates rebuild the inspector once after
selection settles, retaining its existing native focus and field restoration.

Audio **Range…** uses a labelled native operation selector, exact frame fields,
a checkable track list, all-track scope and separate track/master automation
controls. The wrapping form scrolls while OK/Cancel remain outside it. Validation
and target/duration summaries are textual. Timeline range brackets and dotted
boundaries supplement shading, with exclusive bounds in its accessible
description. Automation tables mark retained curves as segments and expose
their original span/offset in tooltips. New strings use `AudioRange` and
`AudioRangeDialog`; no physical input or assistive-technology acceptance is
implied by direct Qt checks.

Level camera resize handles carry X/Y/Z and signed-face labels, with separate
clickable boxes and leader lines at enlarged text scales. Axis identifiers stay
left-to-right inside RTL layouts. The high-visibility theme uses opaque labels,
outlined handles and a dashed selection box. Localized dimensions and validity
appear in status and the accessible description; start/end changes announce an
updated description. Numeric **Resize Selection** remains the keyboard path.
Semantic tests cover registered profiles, enlarged high-contrast RTL labels and
unchanged source state during previews. Native pointer and screen-reader
acceptance remain open. See [camera resizing](LEVEL_EDITOR.md#camera-selection-resizing).
Shared single-line readouts keep their height across changing status text and
bidi isolates. Font/style changes still scale them; text elision, tooltips and
full accessible descriptions remain available.

Session clip selection uses the native extended-selection tree with a textual
Group column. Selection and effective-target counts expose linked membership;
a named **Link grouped clips** checkbox controls expansion. **Selection…**
provides native alternatives to grouped timeline dragging, with labelled
operation/frame/gain/fade/name controls, validation text and persistent OK/Cancel
outside a scrolling, wrapping form. Source strings add `AudioArrangement` and
`AudioArrangementDialog`; group names remain user content. Timeline highlighting
uses outlines as well as colour and its accessible description reports target
count. Direct Qt checks cover focus metadata, scaling and expanded RTL forms;
physical keyboard and assistive-technology acceptance remain separate.

Audio tempo/meter editing uses named native tempo and signature tables,
focusable position/BPM/bar/numerator/denominator fields, explicit Set/Remove
commands, scrollable tabs and inline validation. Forms wrap long labels. The
session's `bar.beat.tick` field provides navigation without timeline gestures;
its accessible description includes current BPM, meter and tick resolution.
The musical ruler exposes its cursor through the existing accessible Graphic
and uses text/dashed markers as well as grid lines. Time and numeric position
syntax increase left-to-right even in an RTL interface. Scaling, expansion and
offscreen native-widget checks do not establish physical screen-reader acceptance.

Session Effects uses an ordered native list, labelled parameter spin boxes,
bypass checkbox, Add/Remove and named earlier/later buttons. Text identifies
enabled/bypassed processors and validation errors; Apply/Cancel stays outside
the scrolling form. Parameter controls expose accessible names and focus, with
wrapped labels for scaling/expanded RTL. Strings use AudioEffects and
AudioEffectsDialog alongside the existing session/CLI contexts. Changes apply
as one undoable draft and stop playback. Direct Qt layout/control checks are
separate from native keyboard and assistive-technology acceptance.

The focusable Presets disclosure reveals a labelled factory selector, preset
name and file controls. Loading replaces only the draft; a textual result
explains Apply. Asynchronous file operations disable editing, expose a named
progress bar and retain Cancel; errors leave the draft intact. Reverb/modulation
parameters use the same labelled native controls. Factory names, descriptions
and file errors add the AudioEffectPreset translation context. Native screen
reader and physical keyboard acceptance remain separate from direct Qt checks.

Automation curves have a named preview, native point table, bounded frame/value
fields, outgoing-curve selector and Add/Remove/Clear controls. Table selection
and graphical selection share the same model. A selected point uses a square
outline; selection does not depend on colour alone. All graph edits have native
control alternatives. Scrollable forms retain controls at expanded text sizes.
Effect parameters have a named selector and Read automation checkbox; validation
and lane counts are textual. Strings add AudioAutomation and
AudioAutomationEditor contexts. Native keyboard/screen-reader acceptance is
still required; direct Qt control checks do not establish it.

The session Routing / Sends inspector uses labelled native destination, gain,
balance, signal-point and enabled controls in a scrolling form. A keyboard
focusable send list and Add/Remove actions expose each route; validation text
explains cycles and disables Apply. Apply/Cancel remain outside the scroll area.
Mute, solo, bus names and send enabled states have text representations. Strings
use AudioRouting and AudioRoutingDialog contexts. Scaling/expanded RTL rendering
and direct Qt controls are fixture-tested; physical keyboard/screen-reader
acceptance remains separate. Routing changes stop playback before applying.

Record / Takes uses native tab, form, checkbox, combo, frame and text controls
inside scrollable pages. Arming and Record are separate focusable actions;
device selection has an explicit empty state. Stop/Cancel stays outside the
scroll area. Capture state, frame counts, queue occupancy, peak hold, overs,
errors and verified-prefix status are text, independent of color. Reduced motion
replaces indeterminate animation with a static progress indicator. Review shows
plain-text paths and digests, explicit prefix acceptance and exact frame/channel
alternatives. Source strings use `AudioTake`, `AudioCapture`, `AudioInputDevice`,
`AudioTakeDialog` and `AudioTakeCli` contexts. Offscreen fixtures cover enlarged
and expanded RTL layouts; native microphone prompts, keyboard/screen-reader
acceptance and OS permission localization still need platform verification.

The gesture editor uses labelled native combo boxes and key-sequence editors,
buddy labels, keyboard-focusable buttons and scrollable Plan/Camera/Camera Keys
tabs. Fields expose profile defaults and stable choices; conflicts appear as
plain text and disable Apply. Restore Defaults and import stage a draft, so
closing can discard it. Perspective-only fields identify why they are disabled
for orbit profiles. None explicitly disables a direction. Escape and Tab remain
reserved for cancellation and focus. Expandable, read-only text details
explain potential navigation/command shortcut overlaps. Read-only stores disable
Apply while allowing drafts and export. Labels wrap with expanded translations,
and tabs/forms follow RTL. Actual shell tests use direct Qt actions and widget renders at 100% and
200% high contrast; physical input and native screen readers still need acceptance.
Hold-key fields explain that release, cancellation and focus/profile changes
end temporary navigation. Plan pan, camera look and pitch controls are available
without changing command shortcuts; Sledge's hold bindings and NetRadiant's
separate defaults are exposed in the same localized catalog and Controls help.
Offscreen navigation tests explicitly bypass desktop-pointer reads and warps.

Levels' native Layout menu, View menu and command search share Maximize/Restore
Active View and Equalize View Sizes. The restore label and check state identify
temporary expansion without relying on color. Viewport-scoped shortcuts leave
inspector and text-field navigation intact; the focused pane is remembered when
a menu takes focus, and restoration returns focus to that pane. The workspace
suite exercises direct Qt focus, visibility, state and rendered menus at 100%
and 200% high-contrast RTL with expanded text. Physical keyboard and native
screen-reader acceptance remain unverified.

Plan and shared Models camera status tags keep the view identity first, use at
most two rows and elide overflow within the pane. Their leading/trailing corners
follow the widget's text direction, including RTL. Optional counts never cover
the view identity. Text uses the widget font independently of other render
overlays; high-contrast tags have an opaque background and visible outline.
Unelided status and accessible descriptions remain available. Layout checks
cover narrow panes, Arabic labels and font scales through 300%; native
assistive-technology acceptance remains separate.

Asset workbench authoring and selection actions are registered commands, available
through Tools, command search and customizable shortcuts as well as header buttons.
The palette identifies Textures, Models or Audio for otherwise similar commands.
Buttons share command availability and translated descriptions; folded headers
retain their current shortcut tooltip and accessible name. The Package menu uses
a named native menu button with keyboard focus. Empty-state action rows stack at
large text scales or with longer translations; content toolbars remain hidden
until their page has content. Empty pages scroll when the full text and actions
need more height. Global Build and Launch toolbar buttons use their named icons
at 150–200% text scale, keeping space for command search. `asset-workbench-ui-smoke` exercises shared command
states, package handoffs and Qt-rendered layouts at 100% dark and 200% high-contrast
RTL with expanded strings. Physical keyboard and screen-reader acceptance remain
part of the release audit.

The Package and Release window gives every focusable control an accessible name,
announces its review state through a status strip, chips and a Problems list whose
rows read as "Blocking:" or "Advisory:", and never relies on colour alone: warning
and error glyphs mark files that replace the game's and blocking problems. Space
ticks or unticks the current item, Ctrl+Enter publishes, and Escape during
publishing cancels it instead of closing the window mid-write. The window sizes itself from the text size within the screen, its
options column scales with the font, and paths, versions and package names stay
left to right in a right-to-left layout. `release-dialog-ui-smoke` checks the
names and the review, notes, publishing and indexing states in dark, light,
high-contrast, 150% and 200% text and right-to-left, with snapshots on request.

The profile browser uses a native search field, wrapping profile list and scrollable
text preview with accessible names. Search includes aliases and engine families;
no results disables **Use Profile**. Previewing a profile leaves preferences
untouched until the user applies it. Workspace tests exercise this flow with
200% text, high-contrast colours, RTL and expanded translations.

The 24 level-editor profiles share the existing native Settings selector and
Levels Controls menu. The searchable Controls reference adds translated,
wrapping adaptation rows with accessible text and full tooltips. Camera
orbit/pan modifiers and mouse-look toggle keys are included in generated help;
fly keys remain local to the camera and stop on focus loss, hiding or a profile
change. Profile descriptors invalidate their translated cache on language
changes. `level-profiles-ui-smoke` uses direct Qt services and widget rendering
to check 100% dark and 200% high-contrast RTL layouts with expanded text.
It does not establish native keyboard or screen-reader acceptance.

GtkRadiant 1.4, 1.5 and QeRadiant use the same translated profile names,
searchable command/gesture rows and wrapping adaptation descriptions. NRC, TB
and explicit GtkRadiant version aliases resolve to canonical IDs without adding
duplicate selector rows. Version differences include area-selection modifiers,
surface-click roles and discrete camera keys. Their regression tests exercise
semantic routing without mouse/keyboard injection; physical-input acceptance
remains open for these additions too.

Plan selection size/readouts and Frame Selection include point-sized objects
and straight lines. Cached geometry refreshes after selection, projection,
visibility and source changes; existing profile shortcuts and accessible controls
remain shared. The selection suite compares fresh and reused widget renders at
100% and 200% high-contrast RTL with expanded text.

Plan brush drawing reuses the model viewport's antialiased CPU edge coverage.
Physical display scale and high-contrast widths/colors refresh its bounded image;
text, focus, primary selection markers and editing previews stay live. Larger
member-marker layers use the separately described background overlay path.
World brushes remain thin, entity brushes heavy, and invalid brushes dashed
with crosses. Exactly coincident drawing edges share a stroke, while their
objects remain separately selectable. `level-plan-wires-ui-smoke` checks actual
physical-pixel rasterization and fresh/reused renders at 100%/200% and RTL.
Member rings use the visible-position budget rather than letting offscreen or
coincident members crowd out visible selections. The primary ring/crosshair and
the complete selection count retain their distinct roles.
Primary selection labels stay within the pane and choose a nearby clear position
outside the crosshair and HUD. Long text elides in the reading direction; the
accessible description keeps the complete object identity. Offscreen anchors and
collapsed panes omit the label. Layout and actual-widget tests cover all pane
edges, RTL, expanded names, 100–300% text and 125–200% physical display scaling.
A theme-matched background protects labels from bright selected geometry;
high-contrast labels use an opaque background and visible border.
Grid images and exact-phase member-ring stamps follow display scale and contrast
without snapping source positions or changing the existing marker shapes.
Selected Quake brush edges and curved patch borders use a thicker dashed stroke
along the actual geometry in every plan projection. This adds a non-color cue
alongside the primary/member markers and resize handles. An entity selection
includes its visible owned geometry; hidden children remain excluded and invalid
brushes retain warning crosses. The separate outline layer follows physical
display scale and contrast preferences. `level-selection-outlines-ui-smoke`
checks these cues at 100% and 200% text, high-contrast RTL and expanded labels,
without injecting input. Native keyboard/screen-reader acceptance remains open.

Larger Quake plan views prepare their images in the background. **Updating view…**
is translated in `MapViewport` and appears in both the HUD and accessible
description until completion. Navigation repositions the previous image without
an automatic animation; newly exposed areas fill when the current image is ready.
Changed source/visibility clears retired images and changed selection clears old
highlights. Primary markers, controls and full-geometry picking remain live.
`level-plan-worker-ui-smoke` checks GUI event-loop progress and the updating state
with high contrast, enlarged text, RTL and 2× physical rendering. Targets above
the bounded image size still use complete synchronous painting.

Grid/member preparation uses an independent worker for large Quake scenes or
selections above 64 objects, including Doom. The same translated **Updating view…**
status remains until both geometry and overlays are current. Navigation may
temporarily reproject old grid/member pixels, while the primary crosshair, label
and resize handles use the current view. Selection/visibility changes remove
old member highlights immediately. Contrast or display-scale changes reject
incompatible overlay pixels. `level-overlay-worker-ui-smoke` covers member-only
pending state, stale-result rejection, fractional physical scales, Doom,
enlarged text and high-contrast RTL without injecting input.

Child plan panes preserve fractional physical-pixel origins when rendering
cached geometry, grids and member markers, keeping them aligned with live
selection and edit handles in scaled split layouts. The same bounded background
path remains available at nonintegral offsets. `level-pane-phase-ui-smoke`
checks parent-rendered child panes at 100–200% scale, including phase-changing
moves and complete patch/warning/selection paths. Native monitor migration and
assistive-input acceptance remain separate from these offscreen pixel checks.

The modeller Collision inspector uses standard labelled Qt controls and an
explicit box list. Numeric axes stack vertically inside its scroll area; shader
and numeric fields stay left-to-right in RTL layouts. Selection uses solid,
thicker outlines versus dashed unselected boxes, plus the named list and viewport
description. Fitting, export and placement use cancellable progress, and collision
does not start animation. Text, tooltips, status and CLI diagnostics are translatable.
Physical keyboard/screen-reader acceptance remains part of the release audit.
Collision selection also uses the component table, synchronized with the
inspector and edge picking. Its mode keeps outlines visible. Transform handles
include axis labels; box scaling labels and the accessible description explicitly
identify local axes. Geometry supplies equivalent numeric transforms, pivots and
snap steps, and each committed gesture is one selection-aware undo action.
The short Box table heading keeps names visible at expanded RTL text sizes.
Enlarged/translated gizmo labels avoid each other within the viewport and connect
back to their handles when displaced.
See [Model Collision](MODEL_COLLISION.md).

Animated collision adds labelled **Edit poses**, **Animate Box**, **Make Static
from Current Frame** and **Fit New Animated Box** controls. Actions retain Qt
button roles and keyboard focus, with wrapping captions at expanded text sizes.
The shared Geometry scope stays synchronized. Inspector/table values identify
stored poses; playback outlines may interpolate, while pause and edits return to
stored data. A textual summary identifies mode and frame; handoff tooltips explain
that the current pose becomes static brushes. Reduced motion suppresses playback.
Native screen-reader and physical keyboard acceptance remain open.

Build preparation uses a scrolling native form with labelled/buddy-linked map
name, target game, destination and size controls, accessible names/descriptions, selectable
details, textual phase/count status and a text-free progress bar. Paths and map
names remain left-to-right in RTL layouts. Reduced motion uses a static busy
state; Cancel closes immediately while the worker discards private preparation.
Use in Build stays disabled until preparation succeeds and freshness checks pass.
Changing the target invalidates the previous prepared handoff. PAK publication
shows its uncompressed state and disables inapplicable compression choices.
All strings are translatable. Offscreen semantic Qt tests and widget renders
cover 100%/200% text, dark/high-contrast themes and expanded RTL labels. These
checks do not replace native keyboard or screen-reader acceptance.

Publish Prepared Build uses a scrolling native form, focusable destination and
compression controls, explicit source/overwrite check boxes, and an accessible
file table paged at 300 rows. It shows textual verification, publication,
cancellation and failure states; progress has no embedded text and respects
reduced motion. A keyboard-focusable Details toggle reports the warning count
and opens the complete review in a bounded, selectable read-only text view;
retained warnings cannot displace the initial package file list. Paths stay LTR.
Cancel requests a safe stop and keeps the result
visible when the worker acknowledges it; forced dialog destruction does not
wait for hashing. Success displays the output, SHA-256 and any backup path.
`LevelBuildPackageDialog`, `LevelBuildArtifacts` and shared CLI strings are
included in extraction. Offscreen semantic tests cover the actual shell flow,
focus policies/roles, 100%/200% text, high contrast, RTL and translation expansion.
Native keyboard, screen-reader and cross-platform acceptance remain open.

Deploy Prepared Build reuses this scrolling review, adding a labelled LTR game
folder, target installation, a labelled PAK slot spin box for Quake/Quake II,
explicit one-operation write permission and optional
launch check box. The computed package path is read-only; status and command
text can be selected by keyboard. The slot's Automatic value has a translated
label and accessible explanation; its range follows the engine. Changing the
folder or slot disables deployment until
Review Again completes; replacing an existing package and writing a read-only
installation require explicit controls. Deployment/launch results distinguish an
already committed package from a later failed/cancelled launch. Core strings use
`LevelBuildDeployment` and `LevelBuildPakDeployment`; CLI shares `LevelBuildCli`. Semantic tests and widget
renders cover the integrated shell flow, role/focus metadata, 100%/200% text,
high contrast, RTL and expansion without input injection.

Offset placement uses labelled native spin boxes, a texture-lock check box,
textual status, a busy indicator and an accessible Details view. XYZ controls
stay left-to-right inside RTL layouts. Scrolling forms reserve translated control
widths at the active text scale. `LevelPlacementDialog` and the shell/core
contexts participate in translation extraction. Semantic Qt tests verify focus
policies and roles and render 100%/200% high-contrast, RTL and expanded text.
Quick Snap, Duplicate, Paste, Mirror and Select Connected Geometry have delayed worker progress with a named phase,
textual object counts and a focusable Cancel button. Escape/Cancel discards the
candidate without waiting for the worker. Progress bars omit embedded text to
avoid clipping at large scales; labels wrap, numeric counts have LTR isolation,
and reduced motion uses a static bar when a total is unavailable. Explicit
offset previews expose geometry, asset, dependency and mesh phases too.
`LevelPlacementTaskDialog` and `VibeStudioLevelPlacement` join extraction.
Semantic Qt tests measure GUI heartbeat and immediate cancellation at 100% and
200% text with RTL, expanded translations and high contrast. These checks do not
establish native keyboard or screen-reader acceptance.

Doom connected selection is a named, keyboard-reachable Edit/command-palette
action, enabled for binary Doom/Hexen vertices, linedefs or sectors. Its tooltip
explains expansion and retained things; refusals name the offending linedef and
the next action. `LevelDoomSelection` joins translation extraction. Mirroring
reuses worker cancellation, textual results and stale-result guards. Semantic
shell tests cover exact undo/save, camera refresh, focus/accessibility metadata,
100%/200% text, high contrast, RTL and expanded labels; native input remains unverified.

The Levels Scene tab uses a native focusable tree with check states, labelled
forms and buttons, plain-text diagnostics, full-name/UUID tooltips and shared undo.
Checkbox changes defer tree rebuilding until Qt finishes its item update, with a
document-revision guard. `LevelScene`, `LevelSceneCli` and the namespaced
`vibestudio::LevelScenePanel` supply translatable strings. Semantic Qt tests inspect
accessible roles/focus and render 100%/200% text, expanded translations, RTL and
high contrast. Native keyboard and screen-reader acceptance remain open.

Scene locks use a named native check box with its standard accessible checked
state. Locked rows include text as well as the control state; inherited locks
show Locked by parent. Protected content remains selectable for inspection.
Refusals name the protected object or node through the shared LevelScene context.

Map opening uses a named native dialog, a middle-elided plain-text source path
with its full value in tooltip/accessibility metadata, a wrapping phase label,
a progress bar with its standard accessible role, and a focusable Cancel button.
Closing or cancelling retains a visible acknowledgement state until the worker
finishes. Messages use the `vibestudio::LevelMapLoadDialog`, `ApplicationShell` and existing
core translation contexts. Offscreen semantic tests inspect roles/focus and
render 100%/200% text, high contrast, RTL and expanded labels. Native keyboard,
screen-reader announcements and OS scaling still require platform acceptance.
See [Background Map Opening](LEVEL_EDITOR.md#background-map-opening).

Doom camera materials use the existing accessible Paint/Sample controls and
explicit floor/ceiling targets. Invalid sector boundaries appear as text in
Details, and namespace/occurrence evidence is available in both the material
report and native dependency tree. Doom inputs have translated kind labels;
flat and wall texture tiles have distinct translated labels, accessible usage
counts and exact source links even when their map names match. Namespace identity
is preserved through thumbnail loading and selection refreshes.
Unsupported asset subset export stays disabled with an explanation in the
report. Offscreen semantic tests cover 100%/200% text, high contrast and RTL.
Native input and screen-reader acceptance remain outstanding.

Material painting exposes named native material/tool combos, a focusable target
field, Paint/Sample controls and Cancel Stroke. Pending counts and cancellation
appear in text alongside highlighted surfaces. Target selectors provide keyboard
access to the camera's atomic service. Escape cancels strokes before returning
to navigation. Messages are localizable. See
[Material Painting](LEVEL_EDITOR.md#material-painting) for Qt test coverage and
the remaining native input/screen-reader acceptance boundary. Profile material
sample/paint gestures have native button/modifier choices, conflict feedback,
shared GUI/CLI references and the same keyboard target alternatives. Material
status updates expose accessible description changes. Instant painting keeps
focus/tool/selection context and adds normal undo. Preference widgets are checked
at 100%/200% text, high contrast, RTL and translation expansion.

The Saved Level Views manager uses native list, text, button and dialog roles,
named controls, focusable actions, plain selectable status and wrapping details.
Its commands are available to keyboard bindings without taking profile keys.
All new UI and validation messages are localizable. Direct Qt tests render
100% and 200% text with high contrast, RTL and expanded labels; they do not
establish native keyboard or screen-reader acceptance. See
[Saved Level Views](LEVEL_EDITOR.md#saved-level-views).

Each orthographic pane exposes a distinct accessible name and the current
projection/selection in its description. The active editing pane has a border
and an **Active** text label; state does not rely on color. Layout and projection
choices are native keyboard-focusable controls, and pane focus changes the
active editing plane. Shared theme, reduced-motion and grid preferences apply
to every pane. Four-view checks use direct Qt calls and widget rendering at
100%/200% text, high contrast and RTL expansion; native input and screen-reader
acceptance remain unverified. See [Four-View Workspace](LEVEL_EDITOR.md#four-view-workspace).

Linked-navigation controls use native checked menu actions, translated names
and explanatory tooltips. Their state is also reported in text and available
through the command palette and configurable key bindings. Linking updates
centres/scale without transferring focus or clearing the active plan tool.
Semantic Qt tests render the Layout menu at 100%/200% text with high contrast,
RTL and expanded labels. Physical keyboard and screen-reader behavior remain
unverified. See [Linked Navigation](LEVEL_EDITOR.md#linked-navigation).

Prefab capture/placement uses named Qt fields, wrapping forms in a scroll area,
keyboard-focusable controls, selectable plain-text status, disabled Apply during
pending/invalid previews, and visible progress/cancellation. Numeric XYZ controls
stay left-to-right in RTL layouts. The shared Models preview respects high
contrast and reduced motion. Direct Qt checks cover 100%/200% text, expanded
translations, RTL and native accessibility roles; physical keyboard and
screen-reader acceptance remains unverified. No input injection is used.

Level Texture Lock, Texture Scale Lock and Allow Valve 220 Conversion are native
checkable Edit actions with translatable labels, descriptive status tips,
explicit check marks and command-palette access. Their persisted state is shared
by numeric transforms and completed viewport operations. Numeric rotation starts
with the same lock/conversion settings. Direct Qt tests cover checked state,
menu accessibility roles, normal/200% text scale and expanded RTL labels; native
keyboard and screen-reader validation remain separate acceptance work.

Cap Patch uses named Qt selectors and numeric fields, a wrapping/scrolling
read-only status view, an indeterminate progress state and disabled Apply while
the draft is pending or invalid. Custom-center coordinates and texture scale
respect text scaling and retain left-to-right numeric editing in RTL layouts.
The Models preview inherits contrast and reduced motion and hatches new caps.
Strings use `LevelPatchCap` and `PatchCapDialog`; native keyboard and
screen-reader acceptance remains a separate requirement.

Stitch Patches exposes named, focusable patch/boundary, direction, target and UV
selectors, a numeric gap limit and a tangent checkbox. Text status, explicit
Apply/Cancel and an indeterminate progress bar communicate pending, failed and
ready drafts. The selectable, read-only status view wraps long words and paths
and supports vertical scrolling. A wrapping form and vertical scroll area support expanded text;
the Models preview inherits contrast and reduced motion and hatches the seam.
Strings use `LevelPatchStitch` and `PatchStitchDialog`. Direct widget tests cover
100%/200% high-contrast RTL layouts; native input and screen readers remain open.

Package temporary-copy preparation uses the named status, byte progress,
read-only diagnostics and focusable Cancel/Close controls in the shared package
operation dialog. Cancel and close wait for worker acknowledgement and discard
the private batch, including a request dispatched after preparation finishes but
before adoption. Named status text distinguishes discarding from finishing a
prepared copy. Cancel becomes disabled during finalization, while Close keeps
the dialog alive until its worker finishes. Failures retain text diagnostics
until Close. Operation
dialogs use a static initial state when reduced motion is enabled. Numeric
progress fractions use locale formatting and directional isolates so RTL does
not reverse completed and total values. Copy fixtures
exercise direct Qt cancellation/close and render dark 100% and expanded
high-contrast light/RTL 200%; native keyboard, drag and screen-reader acceptance
remain part of the release audit. Strings use `VibeStudioPackageCopy`,
`VibeStudioPackageDialog` and `ApplicationShell`.

Temporary Package Copies exposes named, keyboard-focusable native number fields,
live text usage, the session directory and persistent Apply/Close controls.
Limits sit in a scroll area with wrapped labels so 200% expanded RTL layouts
remain reachable. Numeric usage values use locale formatting and directional
isolates. Lowered limits and cleanup failures have textual states; existing
copies are preserved. Usage text changes only when its value changes. Strings
use `PackageCopyBudgetDialog`, `PackageCopyBudget` and `PackageCopyLimitsCli`.
Review Retained Copies opens an asynchronous session table with native selection,
full accessible session IDs, plain-text paths/checksums/errors and named actions.
Unused, live/unavailable and incomplete states have text labels. Discard confirms
the selected session and defaults to Cancel; ownership is checked again on the
worker. Persistent Cancel/Close controls remain outside the scrolling content.
Close requests cancellation and waits for the worker. Reduced motion uses static
indeterminate progress; cleanup becomes determinate when its entry total is
known. Locale-formatted values and technical paths use directional isolates.
New strings use `PackageCopySessionsDialog`, `PackageCopyStore` and
`PackageCopySessionsCli`. Shared initial reservation totals have locale-formatted,
directionally isolated values and explicit incomplete-accounting text inside the
scroll area. Shared Storage Limits opens a scrollable native form with named
spin boxes, wrapped buddy labels, a read-only LTR store path and persistent
Apply/Cancel controls. Lowering limits preserves copies; stale policy reviews
show a refresh diagnostic. Policy writes run on the same cancellable worker.
CLI policy strings use `PackageCopyStoreLimitsCli`. Direct Qt tests/rendering
cover 100% dark and 200% expanded high-contrast light/RTL layouts; native keyboard
and screen-reader acceptance remain open.
Saving a package-derived map uses the standard Save As chooser with a durable
directory suggestion. Choosing this window's disposable copy storage reports
a translatable status message and preserves edits. Code Save As shares this
destination check and retains unsaved text, file identity and read-only state
when refused. An independent save creates the normal editable Code document.
Direct Qt tests exercise
Cancel, destination refusal and a successful independent save without input
injection; native chooser and assistive-technology acceptance remain open.
Direct Qt renders/controls cover scaling and reachability; native assistive
technology acceptance remains in the release audit.

The package inspector exposes named native status/progress widgets and a
keyboard-focusable Cancel Preview / Retry Preview button above all inspector
tabs. Loading, cancellation and errors have text; stale text clears immediately
on selection changes. Labels wrap and the controls inherit text scale, contrast
and layout direction. Reduced motion uses a static initial progress state.
Wrapped Details excerpts are bounded; the Preview tab retains the whole sampled
text with horizontal scrolling. Oversized declared totals use exact positive
decimal bytes with locale grouping in the inspector, including loading,
cancelled and failed text/texture previews. A failed read retains its known
size and no unverified sampled content. Ogg duration estimates, partial stream-position
bounds and invalid/out-of-range page diagnostics are textual, localizable inspector
details. Header timing explicitly describes its limits; it needs no color or motion.
Preview messages use `ApplicationShell`, `VibeStudioPackagePreview` and
`VibeStudioAssetTools`. Worker tests and direct Qt shell tests cover latest
selection publication, cancellation, named controls and 100%/200% expanded RTL
layouts. Native input/screen-reader acceptance remains separate.

Known unreadable package members retain their source occurrence in the planned
browser. Their textual reason remains visible; replacement/deletion repairs the
plan, and Undo restores the unavailable row and export blocker without duplicating
it. This uses existing named editing controls and history diagnostics.
Package recovery reports unavailable original-history counts in textual status,
Activity and chooser details. The warning explains that repaired content is
preserved while Undo may reveal missing bytes. Recovered browser rows expose
read failures and export blockers in text; no color or motion is required.
These strings use the existing package/recovery translation contexts.

Package recovery uses a scrollable Qt chooser with named metadata/history views,
textual verification/incomplete states, labeled interval/byte-limit/copy-limit
settings, logical usage with textual limit behavior, stacked actions and
cancellable background inventory. Restore reuses the package byte-progress dialog;
the browser shows local checkpoint status and Activity retains failures. The
100%/200%, high-contrast, RTL and expanded-string widget tests exercise these
controls without native input. Native screen-reader acceptance remains open.

Package file staging reports retention progress on the existing cancellable
worker dialog. Original import paths remain in staging details; accepted edits
read independent content through the shared package/asset surfaces. Large release
batches queue temporary-file cleanup off the UI thread. The queue drains at
application exit. **Review Lock Files…** uses a named focusable action, a
standard Qt selector and a confirmation with Cancel as the default, full
path/checksum details and worker status. Technical filenames and checksums use
bidirectional isolation; the selector retains raw paths for assistive technology
and service calls. Native owner exclusion also applies after GUI confirmation. Working Import Storage
opens from the recovery chooser and uses labeled, focusable Qt limit controls,
textual usage/error/lease states, full session IDs in accessible table labels and
a read-only details view. A scrollable body keeps Close available at large text
sizes; stacked actions wrap translated text. Inventory/discard use cancellable
workers, and closing waits for their safe completion. Dates and numbers follow
the locale, with directional isolation in table cells. The 100%/200% expanded-RTL
and high-contrast widget tests cover the manager and shared recovery action;
native keyboard/screen-reader acceptance remains open.

Interrupted Saves opens from the package recovery chooser, with named folder,
journal and read-only verification views. Text states and accessible row labels
update after verification; file progress is coalesced on the UI thread. Stacked
actions wrap expanded translations, and a scrollable body keeps Close visible.
Closing requests cancellation and joins the worker. Strings use
`PackagePublicationDialog`, `PackagePublicationInventory` and
`VibeStudioPackagePublication`. Native keyboard and screen-reader acceptance
remains part of the release audit.

Saved Draft Storage opens from the same recovery chooser. Labeled limit controls
share preferences with GUI/CLI saves. Review and reclamation use cancellable
workers, textual verification/in-use/partial-failure states, locale-formatted
usage, a read-only bounded details view and wrapping stacked actions. A scrollable
body preserves the persistent Close control at large text sizes. Storage strings
use `PackageDraftStorageDialog`, `PackageDraftStorage` and `PackageDraftAccess`
translation contexts; plural file counts are extractable. Direct Qt 100%/200%
high-contrast, expanded-label and RTL fixtures cover controls and closed-reader
cleanup. Native keyboard and screen-reader acceptance remains open.

New Package and New Folder use labeled, focusable Qt input controls and existing
theme/text-scale settings. Package folder context menus expose rename/delete;
F2 and Delete in the folder tree act on that folder. Empty new documents expose
Save/Discard/Cancel. WAD disables folder creation, and collisions report textual
status rather than moving only part of the subtree. These strings use the
ApplicationShell and VibeStudioPackageDirectory translation contexts.

Merge Brushes uses a focusable surface list and source selector with textual
Compatible/Choose source/Source chosen states, labelled before/after views and
explicit Apply/Cancel. Conflicting or pending drafts disable Apply. A wrapping,
scrollable form and shared Models renderer inherit text scale, high contrast,
RTL layout and reduced motion. Direct Qt tests render expanded translations at
100%/200%; native keyboard and screen-reader acceptance remain unverified.
Strings use the `LevelMerge` and `LevelMergeDialog` catalogs. See
[Brush Merging](LEVEL_EDITOR.md#brush-merging).

Add Brush uses labelled standard Qt controls for shape, dimensions, position,
axis, detail and material, plus an accessible Models preview. Numeric fields
stay left-to-right in RTL layouts. A wrapping form and vertical scroll area
support expanded translations and 200% text scale; pending/invalid drafts expose
status and disable Apply. Direct widget tests cover those states. Native input
and screen-reader acceptance remain outstanding.

Surface Alignment uses a focusable multi-select Qt face list, labelled numeric
controls and a shared preview. The nonmodal **Surfaces** tab uses standard
focusable Qt buttons, a labelled target combo and named numeric steps. Its
scrollable vertical layout keeps controls reachable at enlarged text sizes;
numeric input remains left-to-right in RTL layouts. Target counts/identities,
queued progress, success, errors and cancellation are textual. Shortcut scope
is limited to level viewports so arrows remain available to fields. All strings
are extracted from `LevelSurfaceTools` and `ApplicationShell`.

Surface clipboard controls use named Qt buttons, a five-mode paste combo
(parameters, world projection, seamless wrap, Radiant values and projection) and an
explicit Valve 220 checkbox with a wrapping buddy label. Copied material/mapping
and target scope appear in text. Status changes announce accessible descriptions;
pending paste reveals the Surfaces tab with progress and Cancel. New strings use
`LevelSurfaceClipboard`, `LevelSurfaceClipboardCli`, `LevelSurfaceTools`,
`ApplicationShell` and the existing profile/gesture contexts. Offscreen semantic
checks exercise 100%/200%, high contrast, expanded labels and RTL without
horizontal panel scrolling. Single-face wrapping announces the clipboard's new
source after successful publication. A named mapping-only checkbox has a wrapping
buddy label and keyboard focus. Selection summaries include patch material targets.
The 84 shared gesture preferences include remappable wrap, selected-value and
mapping-only button/modifier fields with explicit conflict feedback. Radiant
projection adds remappable full/mapping-only gestures and a named panel mode;
the result announces actual edge-on brush mappings in text. Patch projection
shares progress, cancellation, selection and undo with brushes.
Held surface strokes announce preview counts, pending completion, errors and
cancellation through the existing material status label and camera description.
Cancel Stroke remains a named, keyboard-focusable Qt button; Escape also cancels
queued work after release. Live previews respect high-contrast and reduced-motion
settings, and exact Surfaces controls/CLI replay provide alternatives to dragging.
The material status uses a fixed-height elided readout so longer translations
cannot resize the camera during a gesture. Full text remains in its tooltip and
accessible description; Surfaces retains the detailed wrapped status.
The semantic stroke suite renders controls at 100% and 200%, including expanded
translations and RTL; no operating-system input or screen capture is used.
Native assistive-technology acceptance remains open.

The detailed Surface Alignment dialog uses labelled numeric
controls, per-axis alignment selectors and explicit Apply/Cancel actions.
Numeric text stays left-to-right in RTL layouts. A scrollable wrapping form
and the shared renderer inherit scaling, high contrast and reduced motion;
textual status and Material Details expose missing assets and invalid edits.
The `LevelSurface` and `LevelSurfaceDialog` strings enter normal catalogs.
`level-surface-ui-smoke` verifies accessible roles/names, focusability, expanded
labels and direct widget renders at 100% and 200% high-contrast RTL. Physical
keyboard and native screen-reader acceptance still need manual verification.

The level material status uses text counts, a named progress bar and explicit
Cancel/Reload/Details controls. Textures is a named, focusable checkbox; missing
images remain identifiable in textual details rather than colour alone. Source
dimensions stay available even when previews are downsampled. A camera waiting
for current geometry suspends picking to avoid stale object identities. New
strings use Qt translation; the focused UI test uses direct Qt APIs and widget
rendering at normal and 200% scale with high contrast, RTL and expanded labels.
Native screen-reader and physical keyboard acceptance still require manual tests.

Package edit history has toolbar and Edit-menu Undo/Redo controls, with the edit
name in tooltips and status text. On Packages, Ctrl+Z undoes a group,
Ctrl+Shift+Z redoes it, and Ctrl+S saves a portable draft; other surfaces retain
their own shortcuts. Draft save/open uses the shared focusable, cancellable worker
dialog, byte progress, accessible names and textual errors. Closing modified work
offers standard Save, Discard and Cancel actions. These new strings use the normal
translation catalogs. Full screen-reader and platform keyboard acceptance remains
part of the package release audit. Folder creation, rename, delete, unstage and
Undo/Redo use the same worker dialog with a translated edit title, textual record
progress, accessible progress description and focusable Cancel. Cancellation
preserves the current document and history through the final UI handoff. These
controls inherit scale, high-visibility themes, RTL and reduced-motion settings.

The texture browser exposes named Cancel Preview and Reload Previews controls.
Textual loading phases and thumbnail progress accompany background decoding;
Edit Selected remains disabled until the selected result is ready. Cancellation
and stale-result rejection are exercised with direct Qt commands, without
controlling system input. These controls inherit the current theme and scale.

The package browser refreshes staged paths and composition after edits and
undo/redo. Its native Qt list model exposes every matching row with on-demand
labels and accessible text. Background indexing/filtering reports textual record
progress with named, focusable Cancel/Retry controls. Pending and cancelled lists
clear entry actions and previews. Enter in the filter selects the complete result
after preparation. Unreadable payloads have an explicit text label and accessible
diagnostic; status does not depend on colour. Browser strings use
`PackageEntryView` and `VibeStudioPackageBrowser`; folder model strings use
`PackageFolderView` and `VibeStudioPackageFolders`. The native tree exposes full
folder paths and directory warning descriptions through accessible roles. It
shares listing progress until metadata is ready and clears stale identities on
revision changes. Rename/Delete resolve the selected folder even while its entry
list is still filtering. Query changes preserve tree selection and scrolling;
folder labels use the same technical-name RTL isolation. Deep selections remain
readable through horizontal scrolling in both layout directions. Record counts use locale
formatting and item counts use translated plural forms. Technical filenames use
LTR isolation in RTL layouts, preserving numeric names and extensions without
changing their logical paths. There is no animated loading effect. Repeated names carry a textual source-entry label, also included in
accessible row text, and preview resolves the selected occurrence. Unreadable
rows and blocked-edit diagnostics remain visible. Row selection uses source
ordinals where available so refreshes do not switch between repeated names.
Fixed-pitch preview and detail text follow live text-scale changes, including
stylesheet-driven theme updates. Shared theme application skips identical
styles and replaces changed application styles without repeatedly restyling
deeply nested controls. Local widget styling, text and selection survive dark,
light and high-contrast transitions; the nested-widget regression also checks
fixed-pitch detail scaling. Composition bars derive their height from the
current font, and legend rows report height-for-width to their parent layout.
A slice shows its percentage only when the complete label fits. Percentage
labels use a solid theme surface with the normal foreground, preserving light/
dark contrast over colored hatching. Full category
values remain in the legend, tooltip, accessible summary and composition list.
Elided legend text uses first-strong direction isolation in RTL layouts. The
chart regression checks real percentage glyphs, rendered bar height and live
100%/200%/100% transitions through dark, light and both high-contrast themes.
The browser UI check uses direct Qt APIs at
100% and 200%, high contrast, RTL and expanded occurrence labels; native
keyboard and screen-reader acceptance remains part of the release audit.

Export Package Subset uses a read-only Qt table with explicit selected/required
reasons, source-entry labels, accessible row text and full path tooltips. A named
page control exposes every member in batches of 500; exporting includes all pages.
Preparation/export have textual loading, cancellation and result states, and
closing joins the worker before destruction. Scrollable review content keeps
Close outside the scroll area at 200% scale. Headers elide with full tooltips;
buttons wrap expanded translations, and the dialog inherits RTL layout. Strings
use `PackageSubsetDialog` and `VibeStudioPackageSubset` catalog contexts. Direct Qt
checks exercise focusable controls and normal/high-contrast RTL renders; native
keyboard and screen-reader acceptance remains part of the release audit.

Extraction Paths uses a named, focusable Qt table with distinct spoken source
identities and editable relative destinations. F2 edits the selected output
cell; path editors stay left-to-right in RTL layouts. Textual row diagnostics
and a wrapping status explain invalid names or collisions, and disable Extract
until valid. Occurrence edits also identify the source entry in history and
staging labels. The focused UI test covers direct Qt edits and widget renders
at 100% and 200% with high contrast, RTL and expanded translations. Physical
keyboard and native screen-reader acceptance remain separate release gates.

Rotate Selection uses labelled Qt axis/pivot selectors, focusable numeric
controls, descriptive texture-lock options and a textual progress/error state.
Numeric coordinates stay left-to-right in RTL layouts. The form sits in a
scrollable panel, supports wrapping labels, and shares high-contrast/reduced-
motion preferences with the model viewport. Validation disables Apply until
the current asynchronous preview is ready. All new text is translatable;
normal and 200% layouts with expanded labels are exercised through direct Qt
APIs. Native keyboard and screen-reader operation still requires manual testing.

Code's New Text File and Save File As use standard Qt actions and scoped
Ctrl+N/Ctrl+Shift+S shortcuts. Draft tabs have distinct names, non-color modified
state, and updated accessible close-button names after saving. The recovery
browser uses a named list, labelled preference, textual state and progress;
copies restore as drafts. Recovery strings use `TextRecovery` and `CodeRecovery`
contexts. Direct widget tests cover lifecycle and restoration without OS input
injection; keyboard and screen-reader acceptance still require manual testing.

Go to File shows named discovery status, Cancel Scan and Refresh controls, plus
Activity records for background discovery. Standard Qt controls retain focus
metadata; ranking yields during large queries and removes stale activatable rows.
Offscreen render checks include enlarged high-contrast RTL and expanded labels.
Physical keyboard and OS screen-reader acceptance are still manual gates.

The Code Files panel uses a named standard Qt tree, filter and cancel button,
wrapping textual scan state and inline warnings. Its filesystem work runs in a
background worker and rows arrive in short UI batches. Filters use cached metadata;
selection and expanded folders survive refresh. `code-files-ui-smoke` checks direct
widget behavior and renders 100% dark and 200% high-contrast RTL layouts with
expanded labels. Physical keyboard and screen-reader acceptance remain manual.

The Code Index panel uses named standard Qt filter, list, refresh and cancel
controls. Its wrapping status identifies running, ready, partial, cancelled and
failed scans in text; Activity exposes the same operation. Deferred definition
navigation resumes only for an unchanged document and caret. Direct widget tests
cover focus metadata and render the panel at 100% and 200%, including high contrast,
RTL and expanded labels. Physical keyboard and screen-reader checks remain manual.

The brush component editor pairs an accessible Graphic view with a standard
Qt table for vertex, edge and face selection and numeric coordinates. Selected
handles are square and outlined; edge width and surface outlines supplement
color. Properties scroll, forms wrap, and numeric tables stay left-to-right in
RTL layouts. Every action and error is translatable. Programmatic widget tests
and rendered views cover 100%/200% scale, high contrast and expanded RTL labels;
physical keyboard and screen-reader acceptance remain separate manual gates.

The patch editor pairs its custom graphical control grid with a standard Qt
point table, so each point has row/column identity and editable XYZ/UV values.
Selection uses square versus round handles as well as color. The canvas exposes
a graphical accessible role, focus, selection count and keyboard controls;
numeric movement and snapping are also explicit buttons. Properties scroll and
long form labels wrap. New contexts are `VibeStudioLevelPatch`,
`PatchEditorDialog`, and `vibestudio::PatchControlView`. The UI test covers direct
actions and widget rendering at 100% and 200% scale with RTL/expanded labels;
physical keyboard, pointer and screen-reader acceptance remain manual work.

The New Map form uses labelled, named standard Qt controls, a scrollable form
that wraps long labels, and inline validation. Save has a modal progress and
cancellation surface; recovery records include textual state and source paths.
`level-document-ui-smoke` exercises direct controls at 100% and 200% scale,
high-contrast dark, RTL and expanded translations using widget rendering.
Real keyboard/screen-reader operation remains a manual acceptance item.
New translation contexts include `NewLevelMapDialog`, `VibeStudioLevelDocument`
and `VibeStudioLevelRecovery`. Ctrl+N and Ctrl+S are scoped to Levels.

The Code Search Results workbench uses named, focusable standard Qt controls,
label buddies, text status, progress, cancellation, and before/after text
previews. Filters are revealed on demand. New strings use Qt translation
contexts `vibestudio::ProjectSearchPanel` and `VibeStudioProjectText`.
Result text identifies open-document snapshots; details name the encoding.
Path, source-coordinate and code rows retain left-to-right reading order inside
right-to-left panel layouts.
Confirmation and terminal reports distinguish undoable, unsaved document edits
from saved disk changes, without relying on color. Stale results explain why
the search must be refreshed. Shell messages use `vibestudio::ApplicationShell`.
`project-search-ui-smoke` checks direct widget behavior at 100% and 200% scale,
high-contrast dark, RTL, and expanded translations without OS input injection.
Actual screen-reader and keyboard-only navigation remain manual acceptance
checks. See [Project Search](PROJECT_SEARCH.md).

The Code document readout names encoding, BOM, line endings, and save state in
text and exposes the full information through its tooltip and accessible
description when the visible label is elided. Read-only previews explain why
saving is unavailable. External-edit conflicts use a standard Qt dialog with
Cancel as the default. Text-document diagnostics use the `TextDocument`
translation context. See [Code Editor](CODE_EDITOR.md).

Code's Language Server panel uses named standard Qt controls with focus order,
label buddies, wrapped status text, visible Connect/Disconnect and an expandable
arguments/log section. Text states and Activity describe startup, connection,
failure and shutdown. Diagnostic rows identify unversioned reports in text and
disable unverified locations. Definitions reuse the existing picker/navigation.
Completion uses the named Qt list with signature text, accessible descriptions,
plain-text documentation and textual deprecated/related-edit indicators.
Ctrl+Space requests suggestions; Escape cancels, and Enter/Tab accepts one undoable
edit set. Rows follow editor font scaling and layout direction. Loading and
partial/omitted results have status text. Shift+F12 semantic references use the
existing Search Results controls, visible provider and omission counts, progress,
Cancel, source previews and exact-range selection. Stale snapshots report why
navigation is refused; reference results cannot enable replacement. Direct tests
cover inactive unsaved buffers, cancellation and the return to textual search.
Rename Symbol has a Code-scoped remappable F2 command, an accessible native name
dialog and the shared Search Results review. Progress, provider, before/after
spans, cancellation and apply outcomes have text labels. Open-document Undo and
saved-file outcomes are distinguished. Core validation uses `LanguageRename`
and shared `LanguageWorkspaceEdit` translation contexts;
dialogs and review use the existing shell/panel translation contexts. Direct
widget renders cover 100%/200%, high contrast and expanded RTL text; physical
keyboard and screen-reader acceptance remain separate gates.
Code Actions has a Code-scoped remappable Ctrl+. command and a native focusable
list with accessible names/descriptions. Preferred and unavailable states have
text labels; reasons remain readable and unavailable actions cannot be accepted.
The picker uses `CodeActionsDialog`, parser uses `LanguageCodeActions`, and
review uses shared panel translations. Lazy resolution and preview expose
cancellation and textual status. Direct widget renders cover 100%/200%, high
contrast and expanded RTL labels; physical input and assistive-technology checks
remain separate acceptance gates.
Completion rows announce resolving/unavailable states through visible text and
accessible descriptions. Highlighting resolves documentation/imports; tooltips
escape provider markup. Early acceptance waits while Escape or context changes
cancel. `LanguageCompletion` and `VibeStudioCodeEditor` cover parser and popup
strings. Direct tests cover selection changes, cached results, Undo/Redo and
100%/200% high-contrast/expanded RTL rendering; physical input and screen-reader
acceptance remain separate gates.
Document diagnostics expose textual waiting, failure and incomplete states in
Problems. The provider panel names push versus pull mode and offers a focusable
native **Refresh diagnostics** control for retries. `LanguageServer` and
`vibestudio::CodeLanguagePanel` cover the translated protocol errors and controls.
Direct tests check 100%/200%, high contrast and expanded RTL labels; physical
keyboard and screen-reader acceptance remain separate gates.
Completion snippets expose numbered field status, native previous/next/finish
controls and a named choice selector. Selected text and solid/dashed underlines
provide non-color cues; the editor description explains Tab/Shift+Tab and Escape.
Field changes update this metadata and restore the original description on exit.
`CompletionSnippet`, `VibeStudioCodeEditor` and the shell context cover translated
parser errors, status and controls. Direct widget tests cover 100%/200%, high
contrast, expanded RTL labels, choices, Undo and stale document guards; physical
input and assistive-technology checks remain separate acceptance gates.
Parameter Hints has a Code-scoped remappable Ctrl+Shift+Space command, named
native overload selector and focusable read-only signature/documentation views.
The active parameter is numbered in text, bold and underlined; color is not the
only cue. Code remains left-to-right within RTL layouts. Loading/failed/limited
states are textual, and Escape works from the editor or panel children. Close
returns focus to the editor. Documentation shares Quick Info's resource-isolated
renderer. Parser strings use `LanguageSignature`; controls use
`vibestudio::CodeSignaturePanel`, with shell and semantics contexts for actions.
Direct Qt tests cover 100%/200%, high contrast, expanded RTL labels, focus policies
and scoped shortcuts. Physical keyboard and screen-reader acceptance remain
separate gates.

Formatting has remappable document (Alt+Shift+F) and selection (Ctrl+Alt+F)
commands. A named nonmodal Qt progress dialog exposes Cancel and a textual
provider/status; the same operation appears in Activity. Success, failure,
cancellation and no-change results have text status. Direct tests render progress
at 100% and 200%, high contrast and expanded RTL labels, and verify one-step
Undo/Redo without OS input injection. Physical keyboard and screen-reader
acceptance remain separate gates. Parser errors use the `LanguageFormatting`
translation context; commands and progress use `vibestudio::ApplicationShell`.
Quick Info provides a remappable Ctrl+I command and a named, selectable standard
Qt documentation view, so pointer hints have a keyboard-accessible equivalent.
Loading, cancellation, empty replies, errors and shortened/omitted content use
text status. The pane follows text scaling, themes and RTL layout while code
snippets keep their reading order and use studio monospace typography. New
contexts are `LanguageHover`, `CodeQuickInfo` and `vibestudio::CodeQuickInfoPanel`.
Direct tests cover 100%/200% scale, high contrast, expanded RTL labels, focus
metadata and the shortcut registration; physical input and screen-reader
verification remain manual gates.
Strings use `VibeStudioCodeEditor`, `LanguageReferences`, `vibestudio::ProjectSearchPanel`,
`LanguageCompletion`, `vibestudio::CodeLanguagePanel`, `LanguageServer` and shell translation
contexts. Direct widget renders cover 100%/200%, high contrast, RTL and expansion;
physical keyboard and screen-reader acceptance remains a manual gate. See
[Local Language Services](LANGUAGE_SERVICES.md).

The texture Export inspector uses named, focusable Qt controls and only shows
fields relevant to the selected profile. Preview mip level, validation details,
palette provenance and alpha changes have accessible names and text summaries.
Encoding exposes progress/cancellation; publication reports completion separately.
The browser's PNG export has a named progress dialog with a selectable destination,
text status and focusable Cancel/Close controls. Cancellation remains available
during preparation and is disabled while publishing; failed output stays visible.
Direct-command tests and widget renders cover dark and both high-contrast themes,
100%/200% text, RTL and expanded translations. Live assistive-technology and input
verification remain release-audit work, without automated input injection.

The texture editor provides a focusable canvas with arrow-key pixel movement,
Space to apply a tool, Shift-arrow clipping selection, zoom/fit shortcuts, and a
coordinate/RGBA readout. Named Qt controls expose brush, color, transform,
palette, layers, selection operations, and package fields; scrollable properties accommodate larger text.
Line/rectangle/ellipse tools accept two Space commands with arrow movement
between them, expose pending endpoints in text, and cancel without editing.
Brush shape, paint mode, fill tolerance, filled shapes and wrapping controls
enable only for relevant tools. Pointer zoom preserves the pointed pixel;
keyboard zoom preserves the view center. Direct-command tests cover these paths.
Canvas sizing exposes a text preview of the target size and pixel offset.
Selected transforms have separate dimensions and named edge/corner anchors,
with disabled actions and text explanations for missing selections, locked or
hidden layers, and out-of-canvas results. Anchor directions refer to image
coordinates and stay the same in RTL layouts. Standard Qt fields support
keyboard navigation; their commands and expanded layouts have direct API tests.
Layer rows expose names, visibility, locks and opacity in accessible text.
Save Project, Export, and Stage Export use distinct actions and explicit save-state labels.
Save As uses the platform shortcut, with Ctrl+Shift+S when the platform theme
provides no standard binding.
Both texture toolbars include their buttons in Tab navigation. Their overflow
menus have named, focusable buttons when scaling or translation makes actions
collapse. `texture-accessibility-smoke` checks native roles, names, focus policy,
focus-chain membership, scoped document shortcuts and canvas readouts across all
eight inspector sections in dark and both high-contrast themes, including 200%
RTL/expanded text. It reads metadata and calls commands; physical keyboard,
live clipboard and OS screen-reader acceptance remain unverified.
Shared image previews expose the Graphic role; palette swatches expose the
ColorChooser role and update their spoken selected-index/color summary. These
roles apply to package and model previews as well as the texture workflow.
Project preparation exposes the named progress bar and Cancel Operation button.
Cancel stops layer encoding without marking edits saved or resuming a pending
close/open action. The final backup/publication phase hides Cancel and reports
saving until it either succeeds or retains the document with an error.
Native profile controls expose alpha rules, mip previews, metadata, palette
provenance, and textual warnings. Refresh Palette Source is a named Qt button;
its tooltip explains that authored pixels retain their colors. Palette source
changes use the footer's progress and Cancel controls; cancellation and stale
source rejection restore the previous choice without changing saved metadata.
Stage and Apply validates the profile, package destination and map selection.
It supports Quake III PNG/TGA, Quake II WAL paths and native WAD2 miptexture
names; a disabled action explains incompatible paths/profiles in its tooltip.
Package paths explain filename versus WAD lump naming in a tooltip. Restaging
pixels under an already applied reference preserves map undo history.
Checkerboards and contrasting selection/focus outlines accompany explicit
dirty/busy/error status. The Recovery inspector uses named controls for its
preference, interval, checkpoint list and restoration. The footer exposes
background progress and errors in text. Save/Discard/Cancel continuations and
recovery commands are tested through public widget APIs without injected input.
Strings use `VibeStudioTexture`, `VibeStudioTextureOutput`, `VibeStudioTextureProject`, `VibeStudioTextureRecovery`, and
`VibeStudioTextureEditor`. Offscreen tests call commands directly at normal and
200% high-contrast/RTL settings without controlling the user's input.

Package opening, file staging, extraction, comparison, validation, and save dialogs use named standard Qt controls, selectable
text reports, explicit status words, progress, and cancellation. Validation
reports bytes read while a large file is running and offers a named JSON export
action. Review filters,
results, and report actions participate in keyboard focus. The
`package-operation-ui-smoke` test exercises 100% and 200% scale, high-contrast
light, RTL, and expanded translations using offscreen widget rendering. New
strings use the `VibeStudioPackageDialog` translation context; there are no
mandatory animations or cloud dependencies in these workflows.
Source fingerprinting shows the current path and byte progress, accepts
cancellation within a file, and keeps the previous package/plan on cancellation.
Directory indexing and index preparation now use the same progress and Cancel
controls. Their phase text and admission errors are localizable in the archive
context. Limit failures return no partial document; opening preserves the current
package. Offscreen tests gate each indexing phase while UI timers continue and
request cancellation, then verify that no partial archive or staging is adopted.
Editable-base preparation, WAD directory reconstruction, ordering and planned-reader
rebasing now share the control. Staging phase strings are translatable in the
staging context; the open dialog switches to textual records checked before base
preparation. Tests also cancel from UI timers during base metadata and ordering,
and verify that save/reopen cancellation preserves the committed output report.
These changes add no gestures, animations or preference requirements.
Filesystem adapter capture shares the existing indexing progress/cancellation.
Missing or ambiguous backing uses localizable unavailable-entry text and existing
repair/Undo controls. Invalid virtual WAD layout and changed provider size have
text diagnostics; this adds no color-only state or additional control.
Source protection collection reports **Retaining package source protections**;
output matching reports **Checking package source protections** through the same
cancellable task surfaces. Export, extraction, copy and draft progress report
records checked for these metadata phases, including the accessible description.
The summary also shows the count when an unknown total makes the progress bar
indeterminate. Payload reading restores byte units, the matching accessible description and the
previous summary. Finished operations clear the old phase description.
UI timer tests cover cancellation during collection and matching, plus resuming
payload progress, at 100%/200% text scale, expanded translations and RTL. Invalid paths, incomplete provider
declarations, protection limits and save-lock collisions use translatable staging
diagnostics. Draft/recovery refusal preserves the open document; these changes
add no gestures, settings, animation or color-only status.
Combined-session admission and merging use the same localizable archive context;
folder-root diagnostics use the map-assets context. Incomplete texture sources
have a textual warning state and JSON flag, even when a map has no references.
This introduces no new dialog or interaction pattern.
Retained-content admission uses localizable staging/draft errors through the
existing task/error surfaces. Rejection keeps the current document and undo/redo
state; no additional gesture or color-only indicator is required. The manifest
reports generated-byte and payload-hash usage/limits as numeric byte strings.
Retained metadata admission reports localizable record/text-limit errors through
the same surfaces. Group creation and selected unstage can be refused without
changing the document; no new control or gesture is introduced. Reserved
operation slots keep Undo/Redo usable at capacity. The manifest includes logical
record and metadata-byte usage/limits. Worker tests cover group refusal before
source reads and a partially accepted import batch with one reversible undo step.
Snapshot preparation exposes localizable entry and folder phases through existing
worker progress/cancellation. A refused browser view has an explicit unavailable
state, an accessible reason on its tree/list rows and the explanation in the
preview area. Header context, composition and the three asset-browser empty
states carry the same diagnosis, including accessible descriptions; previous
composition slices are cleared. It preserves Undo/Redo. The GUI regression
exercises refusal and
recovery at 100% and expanded 200% high-contrast/RTL; core and CLI checks assert
that a failed view cannot be reported as a successful empty package. Remaining
synchronous GUI view preparation still requires responsiveness acceptance.
Staging sizes distinguish **Size exceeds the supported range** from **Size
unavailable** using localizable text. Oversized before/after compositions explain
why proportions are unavailable and omit their proportional bars. This state
also blocks archive export and remains readable without relying on warning color.
The package entry list stays available for repair; large unsigned entry sizes
have exact positive localized byte counts in display, tooltip and accessible
text. The folder root exposes the aggregate warning in its accessible description.
Overview charts and entry-detail composition explain unavailable proportions;
deletion, Undo/Redo and saving a repaired archive update these states together.
The Save As menu button exposes the checked **Include Staging Manifest** option
through a native Qt action and an explanatory tooltip. Clearing it permits
archive-only repair when a deleted original payload cannot be hashed; retained
output payloads still verify. Its checked state lasts for the current window.
Opening prepares cached summaries on its worker with record progress, an
accessible Cancel control and the phase in the details pane. Cached status and
composition queries reuse that prepared metadata. These changes add no gesture
or preference; native accessibility and full-shell performance acceptance remain
separate release requirements.
The staging list uses native Qt selection/focus with every operation and
diagnostic represented in its model. Compact change rows expose complete text,
including source occurrence, through accessible roles and tooltips; a named,
read-only **Staged change details** pane permits keyboard selection and copying
without relying on hover. The vertical splitter adjusts list/detail space.
Overview/composition rows wrap, while compact rows elide visually. Technical
paths use direction isolates in display text and retain raw exact identities in
action/accessibility roles. State remains textual as well as colored. Snapshot
replacement clears stale details; unchanged refresh retains selection. Direct
Qt tests cover large lists, scales, high contrast and expanded RTL text; native
keyboard/screen-reader acceptance remains required.
History-counter exhaustion uses a localizable staging error on the same failure
surfaces. It explains retained Undo/Redo and draft saving plus export/reopen to
start fresh history; no new control, preference or gesture is introduced.
Installation palette reads use the same worker controls. Automated tests verify
that the UI event loop runs during reads and that closing waits for cancellation.
Extraction exposes the current file and byte progress; its named Cancel action
stops within that file and its report distinguishes completed outputs from the
discarded partial file. The same widget tests render this dialog at 100% and
200% high-contrast/RTL with expanded strings and check focusable cancellation.
Save progress begins with localizable **Checking package output limits…** and
record counts, keeping its accessible Cancel control and the UI event loop active.
This includes internal plan replay, folder preparation and WAD assembly; the
details pane identifies the preparation phase. The wrapping status also shows
record counts while the progress bar is indeterminate; its accessible description
retains the count text. Cancellation discards a partial
view without consuming Undo/Redo. Worker tests gate replay while the UI timer
dispatches, and render its named, focusable Cancel control at both scales.
Refusal puts the index/depth/fingerprint reason at the start of the details view,
ahead of output paths and technical counters so it stays visible at 200% scale;
the current document and Undo remain available. Internal plan row/text/key
refusals use the same localizable error surfaces; folder preflight preserves
redo and leaves the current tree unchanged. A cached unavailable plan retains
its reason until edited or undone. Edit/view admission uses these existing
localizable errors. A grouped import's final plan and browser check runs on its
worker with the existing progress and Cancel controls. Refusal or cancellation
returns no accepted files and keeps the original history; individual editing
errors use the existing status surfaces. There is no new setting or gesture.
ZIP/PK3 structural and filename errors use the existing translatable opening
and validation diagnostics. Strict UTF-8, Unicode Path and CP437 decoding is
shared by the browser, inspectors, asset lookup and CLI. Malformed names are
reported rather than replaced with ambiguous display characters. No new control
or gesture is added.
ZIP folders use canonical
identities so reopening does not duplicate folder rows. This introduces no new
gesture or preference.
Save progress also names source verification, compression measurement, writing,
determinism checks, manifest preparation and publication. Current-file byte
progress remains visible during a large entry; Cancel can interrupt its reads
and compression. The save regression renders partial-file progress at both
scales with expanded text/RTL and checks named, focusable cancellation controls.
Comparison progress identifies the source or staged-result side and current
file in the wrapping status label, keeping them visible above scrollable
source/result context. Per-file bytes remain visible in the progress bar.
Cancellation leaves finished rows available. Widget tests render partial comparison progress at both scales,
including high-contrast light, expanded translations and RTL, and verify the
accessible Cancel action and unclipped progress text.
Native screen-reader acceptance remains a release gate.
No keyboard or mouse input is injected by these tests.

Accessibility and localization are core product design requirements for
VibeStudio. They should be built into the shell, setup flow, editor surfaces,
CLI output, task feedback, AI workflows, and documentation from the beginning.

The goal is simple: VibeStudio should be usable by as many creators as possible
without asking them to fight the interface before they can make something.

## Accessibility Philosophy

- Accessibility is not polish. It is part of the definition of a usable studio.
- Follow [WCAG 2.2](https://www.w3.org/TR/WCAG22/) AA principles where they
  apply to desktop software, then go beyond them when the workflow needs it.
- Respect operating-system accessibility settings whenever possible.
- Make every core workflow possible without relying on color, pointer-only
  gestures, sound-only feedback, tiny text, or hidden status.
- Keep accessibility settings visible during first-run setup and reachable from
  preferences, the command palette, and CLI diagnostics.
- Validate accessibility continuously with keyboard, screen reader, scaling,
  high-visibility, and localization checks.

## Platform Support

VibeStudio should use Qt's accessibility and internationalization stack:

- [Qt Accessibility](https://doc.qt.io/qt-6/accessible.html) for assistive
  technology metadata, scalable UI, keyboard navigation, contrast, sound/speech,
  and screen-reader-friendly widgets.
- [Qt High DPI](https://doc.qt.io/qt-6/highdpi.html) behavior for platform-aware
  scaling and multi-monitor support.
- [Qt TextToSpeech](https://doc.qt.io/qt-6/qttexttospeech-index.html) for
  OS-backed text-to-speech engines. **Not yet a dependency of this build**: see
  [OS-Backed Text To Speech](#os-backed-text-to-speech).
- [Qt internationalization](https://doc.qt.io/qt-6/internationalization.html),
  `QTranslator`, `QLocale`, and Qt Linguist tooling for translation, locale
  formatting, pluralization, Unicode, bidirectional text, and writing-system
  support.

High-DPI behavior is configured before the `QApplication` is constructed:
`configureHighDpiBehavior()` in `src/app/studio_runtime.*` sets the
`PassThrough` rounding policy so fractional scale factors stay exact for the
painted map viewport, charts, and texture previews.

## Visual Accessibility

Required settings and behavior:

- [ ] Follow OS font and scaling defaults on first launch.
  The shell preserves the native UI typeface when applying its theme and uses
  the earlier 10.5-point body text baseline, multiplied by the chosen text scale.
  The System theme follows the desktop's light, dark, and high-contrast modes,
  and the interface language follows the system's on first launch.
- [x] Support application text scale presets: 100%, 125%, 150%, 175%, and 200%,
  and a custom scale path where practical.
- [x] Support high-contrast dark and high-contrast light themes.
- [x] Support a dedicated color-blind-aware status palette and a
  reduced-saturation option as separate, selectable settings: red-green safe,
  blue-yellow safe, and monochrome palettes, and reduced saturation (see
  [Colour Vision](#colour-vision)).
- [x] Offer a thick focus outline, a thick and a steady text cursor, a chosen
  interface typeface, and WCAG 1.4.12 text spacing (see
  [Accessibility Settings](#accessibility-settings)).
- [x] Support UI density presets: comfortable, standard, compact.
- [ ] Ensure toolbars, tabs, cards, inspectors, dialogs, and status chips do not
  clip text at 100%, 125%, 150%, 175%, and 200% scale. Labels that hold paths or
  live readouts now elide instead of widening the window, icons and the mode rail
  grow with the text scale, and every work surface has been rendered at 125%,
  150%, 175%, and 200% through `--ui-snapshot` (with `QT_QPA_FONTDIR` pointed at
  the system fonts, or the offscreen platform draws boxes wider than the real
  glyphs). What that review fixed:
  - Panel tab strips no longer elide to a few letters. Short of room they drop
    their glyphs, then keep only the current tab's label, then show glyphs alone.
    A label not shown stays the tab's tooltip and spoken name.
  - Page header actions fold to their glyphs, the primary one last, so a header
    never holds the window wider than the screen.
  - The tool bar overflow button is wide enough to show its chevron.
  - The Settings category list sizes itself to its longest name.
  - The Workspace dashboard's tiles and cards reflow onto more rows rather
    than scroll sideways, at 175% with a dock open too.
  - The icons the studio does not size itself grow with the text too, through
    the style's small, button, tab, and field icon sizes. That covers menu
    icons, a filter field's glyph and clear button, and the panel tabs' glyphs,
    which had stayed at 16 pixels beside 200% text.
  - Combo box and tree arrows are the studio's own chevrons, sized from the text
    scale. Fusion's arrows stay at 8 pixels whatever the text size.
  - Arrows on a highlighted row, such as a menu item's submenu arrow or a
    selected tree row's branch, take the highlight's text colour, so they keep
    their contrast in every theme.
  - Status messages keep room for about forty characters. Short of that, the
    panel toggles fold to their glyphs, then the status chips fold to a glyph
    and state mark (a check, cross, or triangle, so state never rests on colour
    alone). Each folded label moves into the tooltip, and each chip keeps its
    spoken name and description. A folded Activity toggle's tooltip still
    counts the tasks running.
- [x] Use icons plus accessible labels/tooltips for key actions: the global tool
  bar, page headers, page tool bars, and the mode rail pair a theme-aware glyph
  with an accessible name and a tooltip, and icon-only tool buttons name their
  shortcut in the tooltip.
- [x] Pair color with a glyph, hatch, stroke, or text cue for status in the
  charts, status chips, and map viewport.
- [ ] Audit every remaining surface for color-only state.
- [ ] Provide visible focus states for keyboard and assistive-technology users.
  Every focusable control now draws a focus ring in a colour reserved for focus
  (2px in the high-visibility themes), distinct from the selection fill; the
  audit of every surface is still outstanding.
- [x] Provide a reduced-motion preference that is stored, applied at start-up,
  and settable from preferences and the CLI.
- [ ] Apply reduced motion to animations, transitions, and timeline effects.
  The rail's slide and the page fade honour it; timeline effects are audited
  surface by surface (see Reduced Motion below).
- [ ] Provide preview checks for maps, textures, sprites, shaders, and package
  summaries under high-visibility themes.

### High-Visibility Themes

`StudioTheme` in `src/core/studio_settings.h` carries `HighContrastDark` and
`HighContrastLight` beside `System`, `Dark`, and `Light`. Every theme resolves
to one token set in `src/app/studio_theme.*`, which becomes both the
application `QPalette` and the generated application stylesheet, so dialogs,
menus, tooltips, and docks follow the theme as well as the main window. The
high-visibility themes use pure black and white with a single saturated accent
(yellow on black, blue on white), full-strength outlines on every panel, 2px
borders and focus rings, and a focus colour distinct from the accent so focus
never hides inside a selection. Disabled controls and icons there are a
mid-grey (#8c8c8c on black, about 6:1; #767676 on white, 4.5:1): legible, but
plainly not the full-strength text colour, so a control that is waiting never
looks ready. The `highContrast` flag also reaches every
painted widget: the map viewport, the model viewport, the image/palette/waveform
asset views, the composition, pipeline, and timeline charts, and the code
syntax highlighter.

`studio-theme-smoke` checks all four explicit themes against WCAG 2.2 contrast
ratios on every run: body and secondary text against the frame, page, panel,
and input backgrounds, selection text against the selection fill, and accent
text against the accent fill at 4.5:1; focus rings and success, warning, and
danger colours at 3:1. The default dark theme's list selection is a deeper
burnt orange than its accent for that reason: white text on the bright accent
would fall below 4.5:1. With Qt 6.5 or later, `System` follows the platform colour
scheme through `QStyleHints::colorScheme()`. Earlier Qt versions use the dark
fallback; the four explicit themes remain available.

Shared progress bars keep Fusion's native painting and font-based sizing.
Text uses separate foreground colours over filled and empty regions; the standard
dark fill reserves contrast margin for Fusion's gradient. `progress-theme-smoke`
checks opaque rendered glyph strokes at 4.5:1 in all four themes, at 100% and
200%, in both layout directions and fill directions, at minimum, midpoint and
maximum values (96 combinations). Antialias fringes are excluded from the
foreground measurement. Package operation and map-loading dialogs have no fixed
pixel height cap. The package protection UI regression changes text size in both
directions during extraction and checks readable progress, accessible units,
cancellation controls and completed payload output. This is widget-render and
Qt accessibility-interface coverage; native screen-reader acceptance remains open.
Package byte labels use adaptive binary units and conservative hundredth-unit
rounding so partial work cannot display equal quantities or a full progress value.
The tooltip and accessible description include exact localized byte counts without
64-bit precision loss. Numeric fractions and their units use bidi isolation.
Unknown totals omit the denominator and are identified in accessible text;
metadata retains record units. Validation exposes actual localized file counts
instead of raw progress-format placeholders, and completion clears stale details.
The quantity regression covers C, British English, German and Egyptian Arabic
locales, binary-unit transitions, zero/unknown totals and adjacent uint64 values.

Check boxes and radio buttons are drawn by a proxy style over Fusion from the
same tokens: an outlined box that is clearly visible on every panel, an accent
fill when checked, and a contrasting mark, so a checked state reads from the
mark itself.

`studioStateColor()` in `src/app/studio_charts.*` keeps four tuned ramps — two
high-contrast and two standard — and separates the eight operation states by
both hue and lightness. With a colour-vision palette chosen, the ramps take the
palette's state colours instead, and reduced saturation and monochrome apply to
them as to the theme (see [Colour Vision](#colour-vision)).

### Non-Color Status

Status no longer depends on hue:

- Charts draw each slice with one of eight hatch patterns
  (`patternForIndex()`), and every pipeline stage and timeline row is prefixed
  with a state glyph from `studioStateGlyph()` — a check mark, a cross, a
  warning triangle, and so on, written as explicit code points.
- Status-bar chips and page status strips show the state glyph from
  `studioStateGlyph()` followed by words (a check mark before the package name,
  a warning triangle before "Compilers 7/9", a middle dot before "No project"),
  carry the same text in an accessible description, and expose the state as a
  style property rather than as a raw color.
- Activity, problem, timeline, and build-stage rows lead with the same glyph,
  and their accessible text spells the state out ("Package Scan, Completed.
  ..."), so a screen reader hears the word rather than the symbol.
- The map viewport marks selection with a ring plus a crosshair and a text
  label, draws unsolved brushes with a dashed pen and a cross, and separates
  patches with a dash-dot stroke, so shape and stroke carry the meaning.
- A colour key's swatch sits beside its numbers, never instead of them, and the
  colour picker is an extra: Enter edits the numbers from the keyboard.
- An entity value's suggestions (targetnames for a target, choices from the
  definition) open as a list the keyboard moves through, and are also in the
  editor's accessible description, so they are heard without opening the list.
- The entity inspector lists each declared spawnflag as a check box row in its
  **Spawnflags** group, named for the flag and valued with its bit. The set
  state is the check mark the proxy style draws, and it is exposed to assistive
  technology as the row's checked state, so set and unset survive a color-blind
  or high-contrast reading. Toggling the box rewrites the entity's summed
  `spawnflags` value.
- The entity section of the Levels **Health** tab is headed
  `ENTITIES [<state>]`, with the state word from
  `localizedOperationStateName()`, and prints each finding as its
  `EntityValidationIssue::code` followed by the message
  (`entity-unknown-class`, `entity-required-key-missing`, and so on).
  `m_levelMapValidation` is not one of the lists `applyStateColor()` tints, so
  no row in it depends on hue. Per-row severity is kept only as a data role: it
  is neither painted nor written into the row text, so a reader learns which
  check fired but not how severe that particular finding was.
- The shader stage tree marks a texture missing from the open package with a
  warning glyph and the words "missing from the open package", and one that was
  found with the package it was found in. The model inspector marks the skin
  it drew with a check glyph beside the path, and model warnings sit in their
  own group with a warning glyph on every row.
- The first-run setup stepper marks each step with a glyph (a check for done,
  a chevron for the current step, and a dot for pending, whose text is also
  muted), and each row's accessible text spells the state out: "Step 2 of 8,
  current step: Workspace Profile".

### Reduced Motion

The reduced-motion preference is stored, settable from preferences and from
`--set-reduced-motion`, and read back at start-up. The shell's chrome has two
short animations, both driven by a `QVariantAnimation` and both turned off by
the setting:

- The navigation rail's labels slide open over the page in about 150 ms; under
  reduced motion they open and close in one step (`ModeRail::setReducedMotion`).
- A page the user switches to fades in from the surface colour over about
  170 ms (`PageTransition`); under reduced motion it simply appears. The fade
  never takes input or focus, so it never delays a click or a key either way.

The setting also acts on the surfaces that move on their own:

- `LoadingPane` swaps its indeterminate (marquee) progress bar for a static
  determinate bar while a busy state is in progress.
- The map viewport draws the selection ring solid instead of dashed, because
  fine dashes shimmer while panning.
- The model viewport refuses to animate. `ModelViewport::setReducedMotion(true)`
  drops out of playback if it was running and emits `playbackChanged`, `play()`
  returns without starting, `advanceFrame()` returns early, and
  `updatePlaybackTimer()` stops the `QTimer` that drives frame stepping.
  Stepping by hand with Page Up and Page Down keeps working, because that is a
  deliberate user action rather than motion the widget starts on its own, and
  `statusLines()` says so: "Playback: disabled by reduced motion; step frames
  with Page Up and Page Down". `accessibleSummary()` reports the same state as
  "playback disabled by reduced motion".

The shell pushes the preference into the model viewport wherever it pushes it
into the map viewport, both when a model is shown and when preferences change.
The model workbench's Play/Pause button is disabled under reduced motion, and
its tooltip explains why and that Page Up and Page Down still step frames.

Everything else in the shell is static, so there is nothing further for the
setting to suppress yet. The blinking text cursor has its own switch, **Stop the
text cursor blinking**, since some readers want the cursor still without losing
the other motion cues.

The window title names the current page and project ("Levels — Foundry —
VibeStudio"), which task bars and window switchers show and a screen reader
reads with the window, so the status bar no longer repeats the page name on
every switch.

## Keyboard, Screen Reader, And Assistive Tool Support

The Levels sidebars (`StudioSidebar`) are tab bars and pages Qt already
exposes: each tab has its page's title and description as its accessible name
and tooltip, with its count, and every tab has a **Show** command in command
search for keys of the user's own. Section headings are buttons that report
expanded or collapsed state, toggle with <kbd>Enter</kbd>, <kbd>Space</kbd>,
<kbd>Left</kbd> and <kbd>Right</kbd> (mirrored in right-to-left layouts), and
keep focus where it was. Optional captions name each tab under its glyph for
anyone who prefers words to icons. In right-to-left languages the sidebars
swap sides with the views. Shape tiles, filter boxes and the Transform fields
carry accessible names and tooltips; the counts on filters and badges are
text, not colour. `studio_sidebar_smoke_test` checks the accessible states,
keys, folding and right-to-left mirroring at 100% and 200% text, and the
Levels UI suites check the surface controls at 200% with doubled translations
in right-to-left layout inside the narrower sidebar.

Package menus and toolbar buttons use the same availability policy. Conflicting
controls remain disabled throughout package work, including selection refreshes,
and Open commands become available again after completion, failure or Cancel.
Changing package selection preserves the current selection in other studio
surfaces instead of resetting their models. Native Qt roles and enabled states
remain the accessibility source for these controls.

Textures, Models and Audio share 14 registered authoring, edit, export and
package-reveal commands. Header and empty-state buttons use the same enabled
state and descriptions as menus, custom shortcuts and command search. The
palette identifies each command's module. Actions that need a selection disable
during loading, after deselection, or when a filter hides the selected asset.

Empty-state actions stack and their content scrolls when enlarged text needs
more room. Workbench bodies can scroll independently of their pinned headers
and toolbars, so a wide panel cannot widen every module. Global command labels
and command search have bounded layouts; the
Launch and Test form wraps rows and scrolls without widening every module.
`asset-workbench-ui-smoke` checks all ten empty surfaces and populated asset,
level, code, shader and build surfaces at 100% dark and 200% high-contrast light,
with RTL layout and doubled translations. It checks accessible button identities
and focus policies through Qt; physical keyboard and native screen-reader
acceptance remain manual release checks.

Required behavior:

- [x] Every command in the shell command registry is reachable from the menu
  bar, the command palette, and — where `core/studio_semantics.h` declares one —
  a keyboard shortcut, all generated from that one registry.
- [ ] Bring the remaining per-surface controls into the same command registry.
- [ ] No keyboard traps in modals, dock widgets, editors, setup flow, or preview
  panes.
- [ ] Consistent tab order in setup, preferences, inspectors, and task details.
- [x] The charts, the map viewport, the model viewport, and the asset preview
  views expose accessible names and keep an accessible description in sync with
  their contents, so their data is readable as text.
- [ ] Every other custom widget exposes accessible names, descriptions, roles,
  values, and state changes.
- [ ] Task progress, warnings, failures, prompts, and completion states are
  available to assistive tools, not just visually rendered.
- [ ] The CLI provides accessible plain-text output and machine-readable JSON.
- [x] Editor profile controls document keyboard/mouse changes clearly: the
  Levels **Controls** button names the profile in use and opens a searchable
  list of every gesture and key. Both Levels views say their part of the same
  list after their live description, so a screen reader still hears it after
  the view has changed. The VibeStudio profile is the reset, and a user's own
  keys outrank any profile's.
- [x] The Assistant panel names every part for assistive tools: its first
  line says who would answer or why nothing can be sent, each context row
  reads as its label and size, the status line reports waiting, answered
  (with token counts), cancelled, and failure reasons as they change, and the
  conversation's description carries the latest answer. Ctrl+Enter sends and
  Escape stops a question in flight; the consent dialog opens with Cancel as
  its default button, so Enter never sends by accident.
- [x] The Level and Texture Generators name every control for assistive
  tools (checked by the shell interaction test), take Ctrl+Enter to generate
  and Escape to cancel work in flight, and say what is happening in a
  selectable status line. The level preview carries a description of the
  layout for screen readers and a text legend whose shapes do not rely on
  colour; texture variants carry their seam score as text, and companion
  maps are named under their pictures. Their consent dialog defaults to
  Cancel like the Assistant's. All strings are translatable.
- [x] Edit with AI states each proposed edit's state in words (Ready,
  Blocked, Applied, Failed, Left out) with the reason beside it, not by
  colour; blocked edits stay in the keyboard order with no check box so
  their reasons can be read, Space checks or unchecks the current edit, and
  each row carries its state and reason as an accessible description.
  Ctrl+Enter asks and Escape cancels, as in the generators.
- [x] The Sound Generator names every control (checked by the shell
  interaction test); each variant is listed with its name, length, format,
  and level in words beside its waveform, so the waveform is never the only
  cue, and its details say where it goes and how maps name it. Play and Stop
  are one labelled button; Ctrl+Enter generates and Escape cancels.
- [x] The navigation rail opens its labels as soon as keyboard focus enters
  it, keeps them while the arrow keys move between pages, and hands the
  keyboard back to the page on Escape. Its pin is a checkable button with a
  constant name, and reduced motion turns its slide into a single step.
- [x] The 3D camera can be driven entirely from the keyboard in the TrenchBroom
  and NetRadiant Custom profiles (fly and drive keys, Shift faster and Alt
  slower), and mouse look ends on Escape or when focus leaves the view.

`src/app/studio_actions.*` owns the `QAction` instances. Menus, the toolbar, the
command palette, and shortcuts are all generated from one registration list, and
the default shortcut for a command comes from `core/studio_semantics.h`, so the
documented registry and the running application cannot drift apart. Duplicate
sequences are recorded as conflicts and skipped rather than silently shadowing
an earlier owner. The command palette is a type-to-filter dialog that lists
every enabled registry command plus the documented palette-only entries.

The active editor profile registry exposes routed keyboard and mouse binding
descriptions with stable command IDs in preferences, inspector details, CLI
text, and JSON. Whether a binding routes anywhere is now computed rather than
asserted — see [`docs/EDITOR_PROFILES.md`](EDITOR_PROFILES.md). Reset/revert
actions and full keyboard/mouse audits remain planned.

Current shell custom widgets include a reusable loading pane and detail drawer.
They expose accessible names and descriptions for their title, state,
progress, placeholder, section-list, copy, and detail-content controls; broader
screen-reader and keyboard audits remain required before MVP.
Detail drawer headers reflow their copy/collapse actions when narrow or scaled,
using wrapping native push buttons. Source subtitles elide in the middle and
retain full tooltip and accessible-description text; titles remain plain text.
Selected sections and content survive width, scale and direction changes.
Context refreshes retain text selection and scroll position when the selected
section's content is unchanged. Stable section identities retain their chooser
rows during metadata refreshes and section navigation. Packages preserves the
current preview's text selection during multi-selection and the chosen Details
section when asynchronous preview work completes. Levels keeps the chosen Details section during
selection changes and map edits, while the object inspector follows the current
selection. Shared drawer checks cover this at 100% and 200%, high contrast and
RTL, without native input injection. Real assistive-technology acceptance remains
required.
Shared model/audio/launch metadata grids reserve their styled heading widths
and expose native horizontal scrolling when both headings cannot fit. A heading
minimum does not replace the user's preferred property-column width when text
scales back down. These controls retain standard Qt focus and accessible roles;
offscreen geometry/interface checks do not replace native keyboard or screen-reader
acceptance. Existing translation contexts and settings remain in use.

The Level Editor MVP adds accessible names/descriptions for the map path,
Doom map marker, engine hint, compiler profile, object list, statistics,
preview, validation, save-as, edit, move, run-profile, and CLI-copy controls.
All visible map workbench strings route through Qt translation APIs, while map
format identifiers and CLI flags remain stable technical identifiers.

The Advanced Studio MVP adds accessible names/descriptions for shader script
paths, stage inspection, sprite engine/name/frame/rotation controls, code index
paths, AI proposal kind/prompt controls, extension discovery roots, summary
cards, and detail tabs. Visible shader, sprite, code, AI, and extension strings
route through Qt translation APIs; shader directives, CLI flags, extension IDs,
file paths, and format identifiers remain stable technical identifiers.

The Materials workbench exposes its node graph to screen readers as a list of
nodes (`QAccessibleWidget` children), each named by its title and described by
its subtitle, what feeds it and what it feeds; selection and focus changes are
announced. The arrow keys follow the graph's wires, Enter moves to the node's
properties, Delete removes a node and Alt+Up/Alt+Down reorder stages. Ports
differ by shape as well as colour, library swatches carry shape badges for
animated and rejected materials, node text scales with the text-size
preference through the graph zoom, the flow mirrors right to left, and an
animated preview never starts by itself when reduced motion is on. All
strings are translatable; engine keywords, node ids and file paths stay
technical identifiers.

The studio shell's navigation is keyboard-first. The mode rail is a single tab
stop whose arrow keys, Home, and End move between modes; tool buttons, page
buttons, and the rail take focus from Tab but not from a mouse click, so focus
rings appear for keyboard users without lingering after a click; and the entity
property grid edits the selected value on Enter. Keyboard focus starts on the
work surface rather than on the first tool button. The model and audio
property grids and the shader stage tree are ordinary tree views, so the arrow
keys walk their rows and expand or collapse groups, and every row's tooltip
holds its full, unelided value. The float and close buttons on each dock's
title bar are named for their panel ("Float the Activity panel", "Close the
Activity panel") and take focus from Tab.

The format and UI round adds accessible names for every control it introduces:
the Levels page **Inspector** tab and its inspector list, the entity definition
path field, its Browse and Load buttons and the summary label beneath them, the
**Snap** checkbox on the map viewport control row, the package **Compare**
button, and the model workbench's render-mode combo, animation combo,
Play/Pause button, Frame Model button, Export OBJ button, viewport, and hover
readout. The entity inspector, the entity definition path field, the model
viewport, the model skin preview, and the model details list also carry
accessible descriptions. All of these strings route through `tr()`; entity issue
codes, spawnflag names read out of a definition file, format identifiers, and
CLI flags stay untranslated as stable technical identifiers.

Browser OBJ export reuses the modeller's delayed window-modal progress surface,
accessible status/progress labels and keyboard-focusable Cancel button. The
export action is disabled while its captured frame is being prepared/written;
studio close requests cancellation and resumes after the worker stops. Source
protection and output diagnostics use the `VibeStudioModelExport` translation
context. Omission notes remain available in activity details after completion.

`package.compare`, `model.export`, and `entity.definitions` are registered in
the same shell command registry as everything else, so they appear in the menu
bar and in the command palette. `core/studio_semantics.h` declares no shortcut
for any of the three, so today they are menu- or palette-driven only.

Two viewport details matter for keyboard users. The map viewport binds `Tab` and
`Shift+Tab` to cycling the selection through object tiers, so `Tab` no longer
leaves the widget; `Escape` is the documented way out, and it is staged — it
first cancels an in-progress drag or rubber band and discards the preview, then
clears the selection, and only then calls `focusNextChild()`. The model viewport
takes `Qt::StrongFocus` and does not intercept `Tab`, so focus moves through it
normally.

Reopening the last session at start never moves focus or the page: the studio
opens on the page it closed on, and the status bar says what came back and how
many files could not be found, so a screen-reader user hears the outcome
rather than finding it. The preference sits under **Appearance and Language >
Startup and Recovery**, with the crash report preference.

The notice bar (a crashed previous session, for one) never takes focus: it is
announced as an alert when it shows, its title names the state in words so the
tint is never the only signal, its actions and close button are reached with
Tab, and Esc dismisses it while focus is in it. Its message wraps rather than
elides, so translated text is never cut off.

Doom Builder's raise and lower keys (Page Up and Page Down, Ctrl for ceilings,
Shift for single steps) act only while the map view has focus, so the objects
list and the Inspector keep those keys for paging, and every change is spoken
through the status bar as what it did ("Raise 2 floors by 8").

In the Levels Inspector, Space toggles a flag row from either of its columns,
and an edit keeps the flag row current in its check column, so Space can be
pressed again and again; Ctrl+C copies a Doom or face field's value (a flag as
on or off) as well as an entity's `"key" "value"`.

The Audio page's waveform takes `Qt::StrongFocus`: Left and Right step the
playhead a fiftieth of the sound, Page Up and Page Down a tenth, and Home and
End jump to either end, and its accessible description ends with where the
playhead is. Space plays and pauses while focus is on the sound list or the
waveform, and nowhere else: the filter keeps its spaces, and a focused button,
Loop or Export WAV say, keeps Space to press it. A focused
waveform draws an accent frame, and the playhead is drawn in the text colour
with a notch on the time axis, so it reads without colour. The transport buttons
run the same `audio.playPause`, `audio.stop`, and `audio.loop` commands as the
menu and palette; Play's label becomes Pause while a sound plays, and the time
readout's accessible description says Playing, Paused, or Stopped. A build
without Qt Multimedia keeps the controls, disabled, with a tooltip saying why.

The separate Audio Editor inherits the studio font, palette, contrast, and layout
direction. Its focusable waveform exposes the Graphic accessibility role and
description-change events, frame-accurate Shift selection with dashed boundaries,
single-frame arrows, and an accessible visible range. Labeled zoom/fit commands
and a focusable horizontal scrollbar complement pointer zoom/pan and the overview.
The audio timeline remains left-to-right in RTL layouts. Labeled start/end frame fields
provide an exact keyboard alternative to dragging. Toolbars expose tab-focusable
buttons; Space controls playback only at the waveform, and Ctrl+O/Ctrl+S and
undo/redo shortcuts act within the editor. Ctrl+S saves a lossless audio project;
Ctrl+N creates a new sound, and audio Ctrl+C/X/V apply only at the waveform so
text controls retain their clipboard behavior. Clipboard and corrective effects
are also available through labeled buttons/menus; undo and redo name the edit.
Worker allocation and processing failures appear in the named operation-status
label without replacing the document or history; cancellation remains a distinct
status. Exact-sample no-op checks also run off the UI thread.
Save confirmation stays brief because the destination is already displayed in
the document header; long paths cannot crowd the fixed status area at 200% scale.
Named source, format, view-range, delivery, analysis, placement and recovery labels
also expose their current text through the accessible description. Marker errors
clear that description when the pending values change. This preserves each stable
accessible name without hiding its changing content. Qt interface checks cover
these values and clearing behavior; they do not substitute for screen-reader testing.
Playback also has a named left-to-right seek slider and Playback frame spin box
with a label mnemonic. Both preserve the selection and support paused seeking;
the status reports backend position and precision. Stop remains reachable during
preparation. Loading, buffering, playing, paused and failed states are textual;
backend/device failures leave an actionable retry path. The standard controls
inherit accessible roles and ranges. Direct control tests cover 100/200% dark/
light high contrast, expanded text and RTL; physical keyboard, assistive-tech and
audio-device acceptance remain separate checks. `AudioPlayback` owns translated
backend lifecycle diagnostics.
Deferred loop restarts expose Loading and remain cancellable with Stop. If a
backend cannot seek back to repeat a sound, the status names the Loop-off retry
action; its text and accessible description are checked after Qt's queued layout
update at 100/200% scale with expanded translations and RTL.
The Audio browser uses the same lifecycle controller. Its named transport reports
loading, buffering and playback failure in text, and Stop cancels queued audition
preparation. Preview loading/progress appears in the existing status pane; changing
selection cancels superseded work, and clearing it restores an idle status.
Worker diagnostics use `VibeStudioAudioBrowser`.
The labeled, left-to-right Position slider remains available for compressed media
without a waveform and exposes milliseconds through its accessible description.
Timeline tick spacing follows the font size, with left-to-right numbers and units
inside RTL layouts. Repeated WAD names include a visible entry number.
The Resample dialog exposes a labeled preset, numeric rate, and textual output
frame/duration preview; it supports expanded labels and RTL form layout. Its
worker can be cancelled, and rate/selection changes share the normal undo path.
MP3, FLAC and Vorbis import uses the same cancellable load status and preserves
the previous document on failure. The status explicitly reports omitted container
metadata; native projects and CLI reports retain this warning. Decoder diagnostics
use the `AudioDecode` translation context. Browser WAV export exposes a labeled,
accessible indeterminate progress dialog with Cancel; its description identifies
the atomic-write boundary after which the result is reported as completed or failed.
The audio export dialog has labeled delivery preset and precision choices, a dither checkbox that is
disabled for float output, and a textual format/headroom summary. Both dialogs
keep a scrollable settings body separate from pinned buttons, so translated
summaries remain reachable in short windows. Both are exercised through direct
control APIs and rendered at the three audio test
scale/theme settings; physical keyboard and screen-reader acceptance remain separate.
Ctrl+Shift+E opens audio delivery. Package handoff has a named preset, optional
dither, output summary, and asset/lump path.
**Stage & Place in Level** opens a named, translatable review dialog with
read-only selectable destination fields, labeled game and playback combos,
XYZ numeric controls, target name, and textual entity/error preview. OK/Cancel
stay outside its scrolling body. Validation disables Apply for missing or
mismatched game targets, unsafe paths or inactive speakers without target names.
Direct Qt tests cover stale context and cancellation plus 100/200% high-contrast,
RTL and translation expansion without controlling user input. Physical keyboard
and screen-reader acceptance remains a separate release check.
Markers opens a pending, keyboard-editable cue table with named name/frame
editors, labeled loop bounds, and Use Selection/Add/Remove controls. Validation
errors are textual; OK/Cancel stay outside the scrolling body. The timeline uses
cue triangles/dotted lines and a labeled loop bracket distinct from selection
dashes, plus accessible cue count/loop range. Select Loop offers exact selection
and normal transport audition without dragging. Cue/loop strings use the
`AudioMarkers` and `AudioMarkersDialog` translation contexts.
Analyze opens a read-only, focusable channel table and selectable textual
range/level summaries. Absolute frames and counts identify clipping without
relying on color. Numeric isolation keeps negative signs beside their values in
RTL. Measurement definitions expand through a named, checkable control.
The report inherits language direction, scrolls horizontally
for additional measurements and vertically in short windows, and keeps Close
outside the scroll area. All report labels and measurement definitions use Qt
translation contexts; physical screen-reader acceptance remains a separate check.
For multichannel loudness, a window-modal speaker review has a labeled preset,
named role combo for each channel, a loudness checkbox and live textual
validation. Unknown/repeated roles disable Analyze; opting out keeps true-peak
analysis. Its controls scroll separately from Analyze/Cancel, inherit RTL, and
use `AudioChannelMapDialog` plus `VibeStudioAudioAnalysis` translation contexts.
True peak and LUFS have named report summaries, numeric units and explicit
unavailable/below-gate/not-requested text. The true-peak table column retains
numeric direction isolation. Roles are per-analysis choices, not setup defaults.
Save conflicts retain unsaved work. The labeled recovery
preference and status expose background checkpoints and errors. Recoveries has a
named read-only table, selectable plain-text paths/details, textual verification
states, and focusable restore/discard/refresh controls in a scrollable body.
Close stays outside the body; discard defaults to Cancel in its confirmation.
Recovery verification runs on a worker and reports per-copy failures. Restore
opens an unsaved draft. Startup discovery uses the existing accessible notice
bar without taking focus; an earlier crash notice retains priority. A named
Review Audio action opens the manager, also reachable through File and the
command palette. Getting Started exposes separately named, focusable checkpoint
and startup-offer checkboxes, a selectable folder field, and Review Copies.
The checkpoint choice stays synchronized with an open editor. Direct widget
tests cover focus order, 100/200% high contrast, expanded text and RTL geometry;
physical keyboard and screen-reader acceptance remains open.
Loading, cancellation, errors, clipping,
export, and staged-save state have textual status. Project/recovery errors use
the `VibeStudioAudioProject` and `AudioRecovery` translation contexts. Strings use Qt translation
contexts. The audio UI smoke test covers 100%/200% text, high contrast, RTL,
expanded strings, and the direct control APIs without injecting user input.
The audio document area scrolls at smaller window sizes; operation status,
cancellation, and Close remain outside that scroll area. Both high-contrast
themes are included in the audio layout checks.

`m_levelMapViewport`'s accessible description now covers every gesture:
Shift-click to add, Ctrl-click to toggle, dragging from empty space to
box-select, dragging a selected object to move it, arrow keys to nudge by one
grid step, the wheel to zoom, F to frame the selection, Home to frame the map,
Tab to cycle objects, the Menu key or a right-click for map actions, and Escape
to cancel and leave.

The UI functionality round makes the keyboard reach further:

- A page's own keys fire only while focus is inside that page (Del, F2,
  Ctrl+E, and Ctrl+Z to undo a staged change on Packages; Ctrl+Z and Ctrl+Y on
  Levels; Ctrl+S, Ctrl+L, F3, and Shift+F3 on Code), so a key never acts on a
  surface the user cannot see.
  Map undo and redo and the code editor's save, find, and go-to-line are
  registry commands like the rest, listed in the menus and the palette.
- Switching surfaces moves focus into the new page. Ctrl+F focuses the page's
  search or filter field, or opens the Code find bar, whose Escape returns
  focus to the text; Escape in any filter field clears it. On Settings it
  focuses the settings search. The search keeps the categories whose settings
  mention the text, and Enter moves focus to the first matching control. A
  search that matches nothing is reported in text under the list, not only by
  an empty list. Ctrl+H adds the
  labelled replace field, where Enter replaces one match and Ctrl+Enter every
  match, and the match count and replacement count are the find field's
  accessible description.
- The status chips are buttons in the tab order, each opening the surface
  behind it.
- Alt+Left and Alt+Right go back and forward through the pages left and the
  jumps made, as in a browser, putting the caret, the map selection, or the
  current row back as it was. The status bar says where each step arrived, or
  that there is nowhere to go, and the toolbar's back and forward arrows give
  where they go as their accessible descriptions ("Go back to Levels").
- With several entities selected, a key value they do not share is set in
  italics and read out as differing, not only dimmed, and a spawnflag set on
  some of them is a partly checked box, which screen readers announce as such.
- F4 and Shift+F4 step through the last build's problems from any page,
  showing each where it points, with the status bar saying which it is, or
  why there is none. On a focused combo box F4 still opens its list.
- Enter on a build problem shows the map object or file line it names, and its
  context menu (the Menu key or Shift+F10) copies one problem or all of them;
  Enter on a compiler tool locates its executable, and Delete in the package
  staging list unstages the change.
- The controls this round adds carry accessible names, and the lists carry
  per-row accessible text: the filter fields, the texture and model view bars,
  the Levels **Show** menu (checkable items whose names follow the map's
  game), **Zoom to Selection**, **Use Open Map**, **Show Output**, the
  compiler tool table
  ("VibeMap2 bsp, Ready, path from Automatic"), **Locate…**, **Use
  Automatic**, **Rescan**, and the build problem rows ("Warning: message,
  stage and location"). Build problems and tool states pair a glyph with words,
  never colour alone.
- A leak trail is never colour alone: its ends are a filled circle and a
  hollow square, it carries a **Leak** label, the viewport's accessible
  description adds "A compiler leak trail of N points is drawn over the map",
  and the Health and Problems lists name the leak in words.
- The Workspace **Allow test maps** check box names the installation it acts
  on, and installation rows say "test maps allowed" in words and in their
  accessible text.
- Go to File (Ctrl+P) is keyboard-first like the palette: typing filters,
  Up, Down, Page Up and Page Down move, Enter opens, Escape closes, and each row
  reads as "name, in folder, from source". Go to Symbol (Ctrl+T in Code) is the
  same picker over the open file's symbols, each row read as "name, in line N,
  from kind". Go to Definition is F12 on the name at the caret, not only
  Ctrl+click, and Alt+Left comes back, so neither needs a mouse; the status
  bar names the definition reached, or says none was found. Shift+F12 lists a
  name's uses under Search Results and says how many there are. Ctrl+Space
  opens completions in a list named "Completions", worked with the arrow keys,
  Enter or Tab to choose and Escape to close; with nothing to offer, the
  status bar says so rather than showing an empty list. The Code page's
  Outline tab reads each symbol as "name, kind, line N". Folding needs no mouse.
  Ctrl+Shift+[ folds the block holding the caret, Ctrl+Shift+] unfolds it, and
  Fold All and Unfold All are in the View menu and the palette. The status bar
  says which lines folded or unfolded. A gutter chevron's tooltip says what a
  click would fold. A folded chevron differs from an open one in its direction,
  not only its strength, and the badge after a folded line gives the number of
  hidden lines in words. On Levels, Ctrl+F reaches the Objects filter. Typing a
  query such as tag=3 there and pressing Enter selects every match without the
  mouse, and the status bar says how many. Down in any query filter lists the
  queries used there before, in a list named "Recent queries" that the arrow
  keys and Enter work. Enter in the Packages filter does the
  same for entries and moves focus to the list, where Del and Extract act on them. The inspector's Menu key or Shift+F10
  opens Find Objects With This Value for the current row. F8 and Shift+F8 step through the open
  file's problems
  without leaving the text. The status bar gives each one's position among the
  rest and what it reports, and the diagnostics list selects its row. Sticky
  headers pin the opening lines of the blocks around the view's top. They are
  drawn only, not in the tab order, and the breadcrumb's symbol names the same
  block for the keyboard and screen readers. View > Sticky Headers turns them
  off. The
  breadcrumb above the editor is a row of buttons in the tab order, read as
  "Folder progs", "File defs.qc", and the symbol as its outline row reads. Enter
  or Space opens a folder's menu of files, or Go to Symbol. The shading of a
  name's uses is underlined as well in the high-visibility themes, so it never
  rests on a faint colour alone. The code editor's Ctrl+/, Ctrl+D, and
  Alt+Up/Alt+Down line
  commands are registry commands in the Edit menu too. In the editor Tab and
  Shift+Tab indent text, as every code editor does; Ctrl+Tab and
  Ctrl+Shift+Tab move focus out of it, so open-file tabs switch with
  Ctrl+Page Down and Ctrl+Page Up instead, and Ctrl+F4 closes one. Each tab's
  accessible name is its file name, with "unsaved changes" or "read-only" when
  that applies, and the dot that marks unsaved changes on screen is never the
  only sign.
- The code editor zooms on its own, from 50% to 300%, with Ctrl+= and Ctrl+-
  or Ctrl and the mouse wheel, on top of the app's text scale, so code can be
  read larger without enlarging the whole studio. The level is kept, and the
  readout that shows it is a button whose accessible name gives the level and
  says a press resets it.
- The Levels map box ("Map in the WAD") is an editable combo box: the arrow
  keys choose a map and Enter opens it, and its description says so.
- **Build and Launch** is F5 as well as a button, and the launch form's **Game
  folder** field and copy check box carry accessible names; the check box's
  tooltip says why it is disabled for games that load the built file in place.
- The Levels 3D preview is a picture to look at, not a place to edit: every
  edit it shows stays reachable from the 2D view, the objects list, the
  inspector, and the menus, and the preview's readout names the texture under
  the pointer in words. The 3D Preview command toggles it from the keyboard.
  Selected objects there carry a hatch as well as the highlight colour.
- The clip tool marks the part a cut removes with a hatch as well as a tint,
  says in the status line what Enter will do, and has a keyboard path, Clip
  Selection…, with labelled axis, position, and keep fields.
- The Levels **Create** palette is a labelled, filterable tree: Enter on an
  entry places it in the middle of the view, so dragging onto the map is never
  the only way, and the status line says what was added where.
- A brush's faces are named in words for the way they face (top, bottom,
  north, south, east, west, or sloped with its normal), never by colour or
  position alone, and an edit leaves the keyboard on the edited row.
- A Doom linedef's and thing's flags are check boxes whose rows say what each bit does
  in words, beside its mask; Space toggles the current one, and the status line
  says which flag was set or cleared on which line.
- Draw Sector reports in the status line how many corners are down, how long
  the next edge runs and to where, and when a click would close the shape; the
  first corner is ringed, not only coloured. Its keyboard path is Add Sector…,
  a labelled field of x,y corners, and Enter, Backspace, and Escape finish,
  step back, and leave the tool.
- Resizing the selection by its handles is a pointer gesture; Resize
  Selection… in the Edit menu, the palette, and the map's Transform submenu
  does the same from the keyboard, with a labelled field per axis and a choice
  of what stays in place. Handle hover and drag states are reported in the
  status line in words, not by cursor shape alone.
- The Levels viewport and objects list share one context menu ("Map
  actions"), opened with a right-click, the Menu key, or Shift+F10, holding the
  same commands as the view bar and the inspector. Items that do not apply are
  disabled rather than removed, so the menu reads the same way each time.
- Target links are never colour alone: each has an arrowhead, a
  `killtarget` link is dashed, the selection's links are heavier, and the
  viewport's accessible description adds "Arrows show N target links between
  entities".
- The Levels **History** rows read as "Step 2: Delete entity:3, current,
  saved" (or applied, or undone), so the undo position and the save point are
  spoken, not only drawn; Enter on a row goes to that step.
- The Replace Texture dialog names its fields ("Texture to replace", "Texture
  to use instead") and its live count ("Uses that will change"), so a screen
  reader hears what Replace will do before it is pressed.
- The empty Levels page's **Recent maps** list is labelled and reads each row
  as "name, in folder"; Enter opens the map.
- Enter on a Levels objects-list row frames the object in the view without
  moving focus out of the list.
- Ctrl+A, Ctrl+I, and Ctrl+Shift+A on Levels select every object that is not
  hidden (worldspawn aside), invert the selection, and clear it, and the status
  bar says how many objects are selected.
- [ and ] on Levels halve and double the grid (View menu: Smaller Grid and
  Larger Grid), and the status bar says the new size in words.
- H and Shift+H on Levels hide the selection and show everything again. A
  hidden object's row says "hidden" in its text and its accessible name, not
  only by being dimmed, and the viewport's description counts hidden objects.
  The status lines also read the selection's size ("Selection size: 128 by 64
  units").
- Help > Keyboard Shortcuts is a filterable table of every command's keys;
  each row reads as "command, keys, surface", so the keys a surface adds can be
  learned without leaving the keyboard. Enter on a row changes its keys: Tab
  and Shift+Tab leave the key field as anywhere else, and once a combination is
  in, focus moves on to **Assign** by itself, so Enter assigns it. The line
  under the field says in words which commands the keys would be taken from, or
  why they cannot be used. A command's own keys read
  as "your own keys", and are bold on screen, never marked by colour alone.
- Ctrl+C, Ctrl+X, and Ctrl+V on Levels copy, cut, and paste map objects as
  .map text; text fields on the page keep their own editing keys, and the
  entity inspector's Ctrl+C copies its current key and value.
- Del on Levels deletes the selected map objects, Ctrl+D duplicates them, and
  Del in the entity inspector removes the key on the current row instead. Add
  Entity, Add Thing, Duplicate Selection, and Delete Selection are Edit menu
  and palette commands too, and every add, copy, delete, or key removal says what changed
  in the status bar and can be undone.

`shell-interaction-smoke` presses these keys against the real window in CI.

## MVP Release Audit Status

Milestone 4 adds a release asset gate in
`scripts/validate_release_assets.py`. The gate does not replace manual assistive
technology testing, but it prevents release-candidate packages from omitting
the documented smoke paths for:

- keyboard-only setup completion, skip, and resume checks;
- high-contrast dark and high-contrast light preference coverage;
- text scale checks at 100%, 125%, 150%, 175%, and 200%;
- pseudo-localization and right-to-left Arabic, Urdu, Persian, and Hebrew
  smoke checks;
- colour-vision palettes, reduced saturation, the thick focus outline, and the
  thick and steady text cursor;
- pluralization and translation expansion layout smoke checks;
- OS-backed TTS test phrase and task-result smoke coverage where the platform
  exposes an engine;
- non-color-only state names for project, package, compiler, AI, setup, and
  validation surfaces;
- accessible custom-widget metadata for loading panes, detail drawers, package
  trees, activity tasks, setup controls, preferences, and editor profile
  selectors. Level-map path, object, validation, preview, and compiler-profile
  controls are now part of the manual screen-reader spot-check scope. Advanced
  Studio shader, sprite, code, AI proposal, and extension controls are included
  in the same manual spot-check scope, as are the entity inspector and entity
  definition controls, the model viewport and its playback controls, and the
  package Compare button.

CI additionally runs the shell itself under an offscreen platform plugin with
`--self-test`, which builds every work surface once and pumps the event loop, so
a crash in widget construction or painting fails the build rather than waiting
for a manual pass.

## Accessibility Settings

Settings > **Accessibility** gathers the options below, and Settings >
**Appearance and Language** holds theme, text scale, density, typeface, text
spacing, language, and region formats. Every option applies at once, is stored
with the other preferences, appears in the Settings inspector and
`--preferences-report`, and can be set from the CLI. The first-run setup's
Welcome and Access step opens the Accessibility page directly (**Open
Accessibility Settings**), and command search finds every option's command.

| Option | Choices (default first) | What it does | CLI |
| --- | --- | --- | --- |
| Theme | Dark, System, Light, High Contrast Dark, High Contrast Light | System follows the desktop's light or dark scheme and, with Qt 6.10 or later, its high-contrast mode (Windows contrast themes, macOS Increase Contrast, GNOME High Contrast), live as the desktop changes. | `--set-theme` |
| Text scale | 100% to 200% in 25% steps | Scales every text role, icon, and control; **Larger Text**, **Smaller Text**, and **Reset Text Size** step it from command search. | `--set-text-scale` |
| Density | Standard, Comfortable, Compact | Target size and spacing; Comfortable gives larger click targets. | `--set-density` |
| Typeface | System typeface, or any installed family | The interface font, for example Atkinson Hyperlegible, Lexend, or OpenDyslexic when installed. Code keeps its fixed-width font. | `--set-font` |
| Wider letter and word spacing | off, on | Adds WCAG 1.4.12's spacing: letters 0.12em and words 0.16em further apart. | `--set-text-spacing` |
| Colour vision | Standard, Red-green safe, Blue-yellow safe, Monochrome | Status colours for protanopia/deuteranopia, tritanopia, or no colour at all (see below). | `--set-color-vision` |
| Reduce colour saturation | off, on | Softens the accent, selection, and status colours toward grey without changing their contrast. | `--set-reduced-saturation` |
| Thick focus outline | off, on | A 3px keyboard focus ring on every control, check box, and list, taken out of the padding so text never moves. | `--set-thick-focus` |
| Thick text cursor | off, on | A 3px text cursor (growing with the text scale) in every field and editor. | `--set-thick-cursor` |
| Reduce motion | off, on | No rail slide, page fade, marquee progress, model playback, or shimmering dashes (see Reduced Motion). | `--set-reduced-motion` |
| Stop the text cursor blinking | off, on | A steady cursor everywhere; off restores the platform's blink rate. | `--set-steady-cursor` |
| Status messages stay | Standard, Three times longer, Until the next message | How long a status bar message that clears itself stays readable (WCAG 2.2.1). | `--set-message-duration` |
| Announce results to screen readers | on, off | Asks the running screen reader to say when long tasks finish or fail, and what the status bar reports, without moving focus (Qt 6.8 `QAccessibleAnnouncementEvent`; failures are assertive). | `--set-announcements` |
| Flash the taskbar when background work finishes | on, off | `QApplication::alert` on the window when a long task finishes or fails while another window is in front: a visual signal that needs no sound. | `--set-visual-alerts` |
| Read status changes aloud | off, on | Speaks the chosen events with the computer's own voices (see OS-Backed Text To Speech). | `--set-tts` |
| Read | Long tasks that finish, Failures and warnings, Status bar messages | Which events are spoken; the first two by default. | `--set-tts-events` |
| Voice, rate, pitch, volume | System voice, 0, 0, 100 | The engine's voice and its speaking rate (-10 to 10), pitch (-10 to 10), and volume (0 to 100). **Say Test Phrase** previews them. | `--set-tts-voice`, `--set-tts-rate`, `--set-tts-pitch`, `--set-tts-volume` |

**Read Aloud** (Ctrl+Shift+U, after Microsoft Edge) reads the selected text,
the current line of an editor, the focused list row, or the focused control's
name and description, and pressing it again stops; password and key fields are
never read. **Stop Reading Aloud**, **Accessibility Settings**, and **Toggle
High Contrast** are commands too, so every option is reachable from the
keyboard. Access keys are always underlined: the studio's Fusion-based style
draws them without waiting for Alt.

`vibestudio --cli accessibility report` prints every preference, the speech
engine, and the languages the system asks for (`--json` for the same fields);
`accessibility voices` lists the engine's voices with their ids, and
`accessibility speak <text>` or `accessibility speak --test` speaks with the
saved voice, rate, pitch, and volume or with `--voice`, `--rate`, `--pitch`,
and `--volume`.

### Colour Vision

Each colour-vision palette replaces only the state colours (success, warning,
danger, info) of the chosen theme; text, accent, and focus keep the theme's
contrast-checked values, and every state keeps its glyph, hatch, or word.

- **Red-green safe** (protanopia, deuteranopia) marks success in blue, as the
  colour-blind themes of GitHub's Primer design system do, and keeps warning
  yellow and danger red apart by lightness, since both read as yellows to a
  red-green dichromat. Info becomes a neutral grey.
- **Blue-yellow safe** (tritanopia) keeps danger red against a teal success,
  with a neutral info.
- **Monochrome** (achromatopsia) turns every chromatic colour into the grey of
  the same luminance, so contrast is unchanged and states read from lightness,
  glyph, and word.
- The chart and timeline ramps follow the same choice: results take the
  palette's colours and work in progress takes greys of different lightness.

`studio-theme-smoke` checks every theme with every palette: each state colour
keeps 3:1 against every background of its theme, and a simulation of the
palette's deficiency (the severity 1.0 matrices of Machado, Oliveira and
Fernandes, 2009) keeps success, warning, and danger at least 20 CIE76 units
apart. The standard palette fails that test for red-green readers, which is
why the alternatives exist. Reduced saturation and monochrome mix colours with
their equal-luminance grey in linear light, so every WCAG ratio the themes were
tuned for holds; the test checks that too.

### 3D Renderer Settings

**Settings** > **Appearance and Language** > **3D Rendering** holds the
**Renderer** combo box (accessible name "3D renderer"), a selectable plain-text
**Status** label ("3D renderer status") and **Check Renderers** ("Check 3D
renderers"). Status gives every state in words: in use, available,
unavailable with the reason, not started yet, test image correct or wrong,
and an environment override; nothing relies on colour. The label wraps and
takes the panel's width, so translated text and 200% text scale grow it
downwards. Changing the renderer or finishing a check reports the result in
the status bar, which reaches screen readers and speech like every status
message. A 3D view or material preview that cannot draw says so in its own
text and accessible description, naming this page. New strings use the
`ApplicationShell`, `VibeStudioRendering`, `ModelViewport`,
`MaterialPreviewView` and `RenderCli` contexts.

## OS-Backed Text To Speech

**Status: implemented with the platform's own engines; Windows verified.**

`src/app/studio_speech.*` speaks through the speech engine the operating
system provides, so no voice data ships with the studio and nothing leaves the
machine:

| Platform | Engine | Voices | Rate, pitch, volume |
| --- | --- | --- | --- |
| Windows | Windows Speech API (SAPI 5) through COM | Desktop and OneCore voices from both registry categories | All three; pitch through SAPI's `<pitch>` markup |
| macOS | `say` | `say -v ?` | Rate only; pitch and volume follow the system voice settings |
| Linux and other Unix | Speech Dispatcher (`spd-say`), else eSpeak NG or eSpeak | `spd-say -L`, `espeak-ng --voices` | All three |

The engine starts the first time speech is used or the Accessibility page is
shown, so a session that never speaks never pays for it. When no engine is
found, Settings says why and how to get one, `accessibility voices` exits with
the unavailable code, and every message still has its visual or log
equivalent. `VIBESTUDIO_SPEECH_ENGINE` picks an engine by name (`sapi`, `say`,
`spd-say`, `espeak-ng`, `espeak`), turns speech off (`none`), or selects the
silent `log` engine the tests use, which records what would be said.

What is spoken, when **Read status changes aloud** is on:

- **Long tasks that finish**: an Activity task that completes after running
  for 1.5 seconds or more, with its title, state, and result.
- **Failures, warnings, and cancellations**: any task that ends that way,
  however short.
- **Status bar messages**: off by default. A burst of messages settles for
  450 ms first, only its last message is read, and the same words are never
  read twice running.
- Anything asked for with **Read Aloud**, whether or not the events are on.

Each outcome is spoken once, start-up's own tasks are not read, and a newer
message cuts off an older one (Speech Dispatcher, which a desktop screen reader
may share, is never told to cancel other programs' speech). Screen reader
announcements and taskbar alerts follow the same task outcomes.

- [x] Read selected task summaries, compiler errors, package validation issues,
  AI proposals, and setup guidance: through task outcomes and status messages,
  and **Read Aloud** for anything focused or selected.
- [x] Announce long-running task completion or failure when enabled.
- [x] Let users select voice, rate, pitch, volume, and enabled event categories
  where the OS engine exposes them.
- [x] Provide a test phrase in setup and preferences (**Say Test Phrase**, and
  `accessibility speak --test`).
- [x] Keep visual/log equivalents for every spoken message.
- [x] Never send private project content to cloud voice services: only the
  local engine is driven.

The macOS and Linux engines are driven as separate programs and have only been
checked by code review on this machine; Windows SAPI is exercised with real
voices at volume 0 and with the silent engine in `studio-speech-smoke` and the
shell interaction checks.

## Localization Goals

VibeStudio should be localizable from the beginning, even while translations are
incomplete. Strings should be written so translators can succeed without code
changes.

Engineering requirements:

- [ ] All user-visible UI strings go through translation APIs.
- [ ] Avoid string concatenation that breaks grammar in translated languages.
- [ ] Support pluralization, gender-neutral phrasing where possible, and
  translator comments for technical terms.
- [x] Use `QLocale` for dates, numbers, sizes, durations, currencies, and
  collation.
- [x] Compile `.qm` catalogs during the build and install them with
  `QTranslator` at start-up.
- [x] Set the application layout direction from the selected locale, so Arabic
  and Urdu start the shell right-to-left.
- [ ] Audit each surface — including the map viewport, which does not mirror
  automatically — under a right-to-left locale. So far all three painted
  charts mirror. Activity timeline rows lead with their state glyph and title
  on the right, trail with time and duration on the left, and grow duration
  bars from the right. The pipeline chart runs from the right: the source box
  is on the right, arrows and wrap stubs point left, and each box's glyph,
  label, badge, and active-stage marker lead from its right edge. The
  composition bar fills from the right, and its legend flows from the right
  with each swatch leading its label. Clicks, hover, and the Left and Right
  keys follow the drawn order. The notice bar and loading strips paint their
  state edge on the leading side, and the mode rail paints its divider on its
  trailing edge, beside the page, where a style sheet border-right stayed at
  the window's edge. Its pin and mode glyphs sit as far from its outer edge as
  they do left to right, folded or open: neither layout margins nor style
  sheet padding mirror, so the pin's row swaps its margins (12 px leading,
  8 px trailing) by direction and the entries are padded 7 px on both sides;
  right to left, the pin also places its own icon, because a tool button
  rounds a centred icon's odd spare pixel to the left. The Activity, Inspector,
  and Assistant panels dock on the trailing side, the left, across the page
  from the rail, which keeps the window's right edge; `QMainWindow` keeps its
  dock areas by side, so the shell picks the area by direction
  (`trailingDockArea()` in `studio_docks`). The settings keep the window state
  as a left-to-right window lays it out, and a right-to-left session mirrors it
  (`windowStateForDirection()`), so panels keep their side of the page
  whichever language saved them: the default arrangement opens on the left, a
  panel the user moved beside the rail stays beside it, panels side by side
  along the bottom swap order, tab groups keep their tabs and the one in front,
  and floating panels keep their place on the screen. Reset Layout docks the
  panels on the left, and a change of direction under a running window mirrors
  them. Each panel's title bar keeps the title's wide margin on its leading
  side, and the Inspector's toggles and a floating panel's dock button show a
  sidebar on the left. Painted chart text is first-strong isolated,
  so untranslated English keeps its ellipsis at its end and a number keeps its
  unit after it ("0 ms", not "ms 0"). The timeline's and pipeline's state glyph
  columns widen with the text size, so a 200% glyph is neither clipped nor
  pressed against its text, and a pipeline box widens with its glyph column so
  the label keeps its room. `studio-charts-smoke` compares left-to-right and
  right-to-left renders of all three charts at 100% and 200% text and checks
  where clicks land. It paints them with `grab()`, which starts each painter in
  the application's layout direction as on-screen painting does (`render()`
  into an image starts it in the widget's), so a chart that never sets its
  painter's direction fails. `ui-primitives-smoke` checks the notice edge and
  margins, that the rail's divider sits beside the page, and the panel title
  bar's margins, in both directions. It also mirrors a saved window state with
  a tab group, a closed panel, a floating one, panels side by side, a corner,
  and a tool bar, checks that each lands where a mirrored window puts it, and
  that mirroring twice gives back the same bytes. `shell-interaction-smoke`
  turns the shell's direction both ways with its panels open, with one moved
  beside the rail, and through Reset Layout. `ui-primitives-smoke` also turns
  the rail right to left, folded and open, pinned or not, at 100% and 200%
  text, and checks that its pin and glyphs keep their distance from its outer
  edge; it turns the application too, because a leading glyph aligns by the
  application's direction. A Qt 6.10.1 offscreen audit on 2026-10-07 compared
  English snapshots with horizontally flipped Arabic frames at 1600 × 1000,
  including the shell's seven menus opened by a scratch driver. The asymmetric
  `QMenu::item` padding already preserves its icon and text columns, and
  `studioToolBar` already mirrors its child positions; neither rule needed a
  change. The `runCommand` buttons' contents were 2 px too far from their
  leading edge. Their padding now swaps only for `layoutDirection="1"`, with
  a re-polish on `LayoutDirectionChange`; their widths and LTR padding stay
  the same. `searchField` is not currently assigned to a shell field; a probe
  with it assigned confirms the base 8 px horizontal padding and the focused
  content rectangle already mirror, including a thick focus ring. The command
  search's custom painter already places its own leading and trailing insets.
  `ui-primitives-smoke` checks the four audited rules at 100% dark and 200%
  high-contrast light, on startup, live direction changes, and return to LTR,
  changing the application direction too. An A/B build linked the original
  theme object ahead of the same app library and ran alongside the fix with
  the same project and settings paths: all 16 LTR PNGs were byte-identical.
  Ordinary icon-only tool buttons still have Qt's separate 1 px centring
  rounding offset; this audit does not extend the rail pin's workaround.
  Still to mirror: the Levels inspector's own tab
  shows a sidebar on the right (the `inspector` glyph) though its pane moves
  to the left.
- [ ] Leave expansion room in layouts for longer translated text.
- [ ] Keep file formats, technical identifiers, paths, compiler flags, and code
  snippets untranslated unless they are explanatory prose.
- [x] Allow language selection in setup and preferences, with a restart prompt
  only if live switching is not yet implemented. The Language list (Settings >
  Appearance and Language, reached from the setup's Welcome and Access step)
  starts with **System language**, the default, then every language by its
  name in the interface language and its own name. Choosing a language other
  than the running one shows a notice with **Restart Now**, which closes
  through the usual save prompts and starts the studio again in that language
  with the session reopened.
- [x] Provide pseudo-localization and right-to-left test modes.

### Translation Context

`ApplicationShell` now declares `Q_OBJECT` and is run through `moc`
(`app_moc_headers` in `src/meson.build`), as are the map viewport, the model
viewport, asset views, charts, syntax highlighter, and command palette. Before
that the shell inherited `tr()` from `QMainWindow`, so its strings were
extracted and looked up under the `QMainWindow` context and could never match a
catalog entry written for the shell. Shell strings now resolve under their own
class context. Core modules that are not `QObject`s use explicit
`QCoreApplication::translate("VibeStudio…", …)` contexts instead —
`VibeStudioEditorProfiles`, `VibeStudioRuntime`, and siblings — so every string
has a stable, intentional context. The modules added this round follow the same
rule: `VibeStudioEntityDefinitions`, `VibeStudioModelMesh`,
`VibeStudioPackageCompare`, and `VibeStudioDocumentWatch`.

`ModelViewport` is listed in `app_moc_headers` alongside `MapViewport`, so the
strings it paints and reports — status lines, the accessible summary, and its
empty and no-geometry states — are extracted under their own class context
rather than a base class's.

**Literals lupdate can see.** `lupdate` reads only a literal written inside
`tr()`, `QCoreApplication::translate()`, or a `QT_TRANSLATE_NOOP` marker. It does
not follow a literal into a function. Most modules in `src/core` and ten in
`src/app` used to translate through a local helper such as `mapText(const char*)`,
which called `QCoreApplication::translate("VibeStudioLevelMap", source)`. The
helper translated at run time, but none of its roughly 4,000 literals reached a
catalog, so no translation could ever apply to them. Those call sites now call
`QCoreApplication::translate("<context>", "...")` directly, as
`src/core/ai_transport.cpp` already did, and the helpers are gone.

Text kept in tables and translated later is marked with
`QT_TRANSLATE_NOOP("<context>", "...")`, using the same context as the call that
translates it. This covers known compiler issues (`knownIssue`), Quake map
preflight messages (`addIssue`, context `VibeStudioQuakeMapPreflight`), the
Levels command table, and generated palette
names. lupdate's `-tr-function-alias` is deliberately not used: it would file
these strings under the wrong context.

`scripts/extract_translations.py --check` keeps the gap closed. Any function,
named lambda, or macro that hands one of its parameters to a translation call,
directly or through another such helper, counts as a translation helper. The
check fails on:

- a literal passed to a helper: translate it where it is written, or mark it
  with the helper's context;
- a marker whose context differs from the helper's, which would leave the
  catalog entry unused;
- a translation call whose source is an expression rather than a literal
  (`cond ? "a" : "b"`), or whose context is not a literal;
- a `%n` string translated without a count, or marked with
  `QT_TRANSLATE_NOOP` instead of `QT_TRANSLATE_N_NOOP`, which would file it
  without plural forms.

The check runs in `meson test` as `translation-extraction-validation`, in the
local gate, and in CI. It also runs before `--write` touches a catalog.

Newer shell strings use Qt plural forms where a count is involved, for example
`tr("%n difference(s) between the two packages.", nullptr, differences)` and the
`ENTITIES [%1]` header on the Health tab. There are still no `//:` translator
comments anywhere in `src/app` or `src/core`, so the pluralization checklist
item above, which also covers translator comments for technical terms, stays
unticked.

This fixes the lookup, not the coverage. Every literal passed to a translation
call now reaches the catalogs. However, user-visible text that never goes
through a translation call at all, such as a message built with
`QStringLiteral`, is not something extraction can detect. So the first checklist
item above stays unticked until an audit finds none.

### Catalog Loading

`installStudioTranslations()` in `src/app/studio_runtime.*` is called from
`main()` before the shell is constructed. It removes any previously installed
VibeStudio translator, then searches these directories in order:

1. `$VIBESTUDIO_I18N_DIR`, when set;
2. `<application dir>/i18n`;
3. `<application dir>/../i18n` (the development build layout);
4. `<application dir>/../share/vibestudio/i18n` (the installed layout);
5. `<application dir>/../../i18n` (the portable package layout);
6. `<working directory>/i18n`.

The requested language is first resolved to a target (`system` to the
operating system's language, `zh-TW` to `zh-Hant`, `es-MX` to `es-419`, and so
on; see [Supported Languages And Regions](#supported-languages-and-regions)).
Within each directory it tries `vibestudio_<target>.qm`, then the base
language, so `es_419` falls back to `es`. File names always use underscores
(`vibestudio_pt_BR.qm`); until October 2026 the loader looked for the hyphenated
id (`vibestudio_pt-BR.qm`), so no regional catalog could ever load. The source
language (`en` or an empty locale) installs `vibestudio_en.qm`, which holds only
English plural forms (see below), and skips Qt's own catalogs. For any other
language, Qt's `qtbase_<locale>.qm` is installed alongside the studio catalog so
standard dialogs and buttons are translated too. Failures are collected as
warnings and the application continues in the source language rather than
refusing to start.

`i18n/meson.build` compiles every checked-in `.ts` file with `lrelease` into
`<builddir>/i18n` and installs the results to
`<datadir>/vibestudio/i18n`. `lrelease` is optional: when it is not found, Meson
prints a message and the application runs in the source language.

Portable packaging discovers those compiled catalogs from the selected Meson
build and includes them in its checksums and ZIP. Release artifact steps pass
`--compiled-translations <builddir>/i18n`, requiring all 48 files before replacing
an existing package. The `compiledLocalization` manifest field distinguishes
complete, partial and unavailable compiled sets. Catalog availability is
separate from translation completeness; see [Packaging](PACKAGING.md).

Windows runtime staging merges QtBase/Multimedia messages into
`bin/translations/qtbase_<locale>.qm`, matching the startup loader's prefix.
`qt_runtime_catalog_probe` checks native catalog loading and standard-button
translations against the selected SDK, plus application catalog loading and
English Audio plurals. These deployment checks do not replace human translation
review or native assistive-technology acceptance.

`applyLayoutDirectionForLocale()` sets `Qt::RightToLeft` for right-to-left
locales — including ones outside the shipped target set — and is likewise called
from `main()`.

**Han-script fonts.** Chinese, Japanese, and Korean share the Han characters
but draw many of them differently, and a font made for one may lack another's
forms. Qt picks a fallback font for a script by the operating system's
language, so a Japanese interface on an English system could take its kanji
from a Chinese or Korean font: the wrong shapes, or empty boxes. For a `ja`,
`zh-Hans`, `zh-Hant`, or `ko` interface, `installStudioTranslations()` puts
that language's own interface fonts ahead of the platform's fallback for its
scripts (`QFontDatabase::addApplicationFallbackFontFamily`, Qt 6.8 and later):
Yu Gothic UI, Meiryo UI, Hiragino Sans, or Noto Sans CJK JP for Japanese;
Microsoft YaHei UI, PingFang SC, or Noto Sans CJK SC for Simplified Chinese;
Microsoft JhengHei UI, PingFang TC, or Noto Sans CJK TC for Traditional
Chinese; Malgun Gothic, Apple SD Gothic Neo, or Noto Sans CJK KR for Korean;
whichever are installed (`interfaceLanguageFontFamilies()` lists them). Other
languages keep the platform's fallback. Snapshot runs on the offscreen
platform also load the TrueType collections (`.ttc`) in `QT_QPA_FONTDIR`,
which Qt's offscreen font database skips; Windows keeps its Chinese,
Japanese, and Indic interface fonts in collections, so without them those
snapshots showed empty boxes.

Both of these run once, at start-up: strings are translated as widgets are
built, so a new language takes effect after a restart, which Settings offers
(see the language-selection item above). `activeInterfaceLanguage()` reports
the language the running window was built in.

**Region formats** are chosen apart from the language: **System regional
settings** (the default, the operating system's own number, date, and time
conventions), **Match the interface language**, or any of the roughly 900
regional locales Qt knows, each listed by its own name (`Deutsch (Schweiz)`)
with a sample of how it writes numbers, dates, and times. The choice is applied
through `QLocale::setDefault()` at start-up and as soon as it changes, so
English menus can show Swiss dates. `--set-region` sets it from the CLI.

### Catalog Contents

**English plural forms.** Source strings mark plurals the Qt way, for example
`tr("%n item(s)", nullptr, count)`, and Qt only resolves `%n` into a singular or
plural form through a translator. Without an English catalog the studio showed
"1 item(s)". `scripts/english_plurals.py --write` asks `lupdate -pluralonly` for
every plural message and writes English singular and plural forms into
`i18n/vibestudio_en.ts`, derived from the markup between `%n` and the next
`%1`..`%9` placeholder (`entr(y)(ies)`, `item(s)`, `match(es)`, and bare plurals
such as "%n entries") plus a short list of sentences whose verb also changes.
Without `--write` it checks that every plural message has finished forms, and
that check runs in the gate as `english-plurals-validation`. The forms are
written where lupdate put each message, and `lconvert` writes the file back in
lupdate's own format, so the two scripts never reformat each other's output. A
plural that is marked for later translation needs `QT_TRANSLATE_N_NOOP`: lupdate
files a `QT_TRANSLATE_NOOP` literal as a plain string, with no plural forms to
fill in.

**The other shipped catalogs list every source string, untranslated.**
`python scripts/extract_translations.py --write` runs lupdate over `src/` into
every catalog, with relative source locations and `-no-obsolete`. As a result,
each `i18n/vibestudio_*.ts` file holds every extracted message under its context,
marked `type="unfinished"`. `lrelease` compiles a message only when it has
text: an empty unfinished message is skipped, but an unfinished message that
carries a draft is compiled (the build does not pass `-nounfinished`), so
drafts show in the interface while Qt Linguist still marks them for review.
The localization report counts those drafts apart (`draftedCount`).

After adding or changing strings, refresh the catalogs with
`extract_translations.py --write` and then `english_plurals.py --write`, both
with Qt's bin directory on `PATH`. Running `extract_translations.py --write`
again afterwards changes nothing.

The pseudo-localization catalog declares `en_XA`, CLDR's pseudo-accent locale.
lupdate refuses to update a catalog whose language it has no plural rules for,
and the earlier `qps_PL` code was one of those. So far the catalog carries a
single pseudo-translated sample.

The active localization scaffold lives in `src/core/localization.*` and is
shared by preferences, tests, CLI reports, and diagnostic bundles. It defines
the 47-language target registry, resolves regional and legacy locale IDs and
the system language, lists the region formats, identifies Arabic, Urdu,
Persian, and Hebrew as right-to-left targets, generates pseudo-localized and expansion
stress samples, emits `QLocale` formatting and pluralization samples, checks
expanded text against representative shell layout budgets, resolves the catalog
root and the ordered `.qm` candidates for a locale, reports which `.ts`/`.qm`
pairs are actually present, and inspects Qt `.ts` catalogs for missing,
unfinished, obsolete, or vanished translations. Its pluralization report
distinguishes "the count was substituted" from "translated plural forms came
from a translator", so a stub catalog cannot read as a pass.
`scripts/extract_translations.py` scans the sources for literals lupdate cannot
see (see Translation Context). It then dry-runs Qt `lupdate` against the source
tree and catalogs, so extraction drift is visible before release.

## Supported Languages And Regions

VibeStudio's interface targets 47 languages. A language is on the list when it
meets at least one of these tests:

- **Reach:** about 50 million or more total speakers in the
  [Ethnologue 200](https://www.ethnologue.com/insights/ethnologue200/), with a
  standard written form used for desktop software.
- **Game development market:** it is a language PC game stores localize for
  (the [Steam supported languages](https://partner.steamgames.com/doc/store/localization/languages)
  list), since VibeStudio's users build and mod games.
- **Written standard:** one language written two ways in software, where a
  reader of one cannot be expected to use the other: Simplified and
  Traditional Chinese, Brazilian and European Portuguese, and Spanish as
  written in Spain and in Latin America.
- **Script coverage:** Hebrew, so a right-to-left script other than Arabic's
  is part of every layout check.

Spoken varieties without their own written software standard are served by the
written language their speakers read: Cantonese by Traditional Chinese, Wu by
Simplified Chinese, Egyptian and Levantine Arabic by Arabic, Bhojpuri by
Hindi, Western Punjabi (Shahmukhi) by Urdu, and Javanese by Indonesian. Review
the list against Ethnologue and user requests at each release.

| Code | Language | Native name | Script | Notes |
| --- | --- | --- | --- | --- |
| en | English | English | Latin | Source language and fallback. |
| zh-Hans | Chinese (Simplified) | 简体中文 | Han (Simplified) | Mainland China, Singapore; also Wu. |
| zh-Hant | Chinese (Traditional) | 繁體中文 | Han (Traditional) | Taiwan, Hong Kong, Macau; also Cantonese. |
| hi | Hindi | हिन्दी | Devanagari | Also serves Bhojpuri readers. |
| es | Spanish (Spain) | Español (España) | Latin | Spain and Equatorial Guinea. |
| es-419 | Spanish (Latin America) | Español (Latinoamérica) | Latin | The Americas and US Spanish. |
| ar | Arabic | العربية | Arabic | Right-to-left; Modern Standard Arabic. |
| fr | French | Français | Latin | France, Canada, Belgium, Switzerland, Africa. |
| bn | Bengali | বাংলা | Bengali | Bangladesh and India. |
| pt-BR | Portuguese (Brazil) | Português (Brasil) | Latin | |
| pt-PT | Portuguese (Portugal) | Português (Portugal) | Latin | Portugal and Lusophone Africa. |
| ru | Russian | Русский | Cyrillic | |
| id | Indonesian | Bahasa Indonesia | Latin | Also serves Javanese readers. |
| ur | Urdu | اردو | Arabic (Nastaliq) | Right-to-left. |
| de | German | Deutsch | Latin | Long-string layout stress case. |
| ja | Japanese | 日本語 | Han, Kana | CJK line breaking. |
| pcm | Nigerian Pidgin | Naijá | Latin | West Africa. |
| mr | Marathi | मराठी | Devanagari | |
| vi | Vietnamese | Tiếng Việt | Latin | Stacked diacritics. |
| te | Telugu | తెలుగు | Telugu | |
| ha | Hausa | Hausa | Latin | West Africa. |
| tr | Turkish | Türkçe | Latin | Dotted and dotless i casing. |
| pa | Punjabi | ਪੰਜਾਬੀ | Gurmukhi | India. |
| sw | Swahili | Kiswahili | Latin | East Africa. |
| fil | Filipino | Filipino | Latin | Philippines (standard Tagalog). |
| ta | Tamil | தமிழ் | Tamil | India, Sri Lanka, Singapore. |
| fa | Persian | فارسی | Arabic | Right-to-left. |
| ko | Korean | 한국어 | Hangul | |
| th | Thai | ไทย | Thai | No spaces between words. |
| ms | Malay | Bahasa Melayu | Latin | Malaysia, Brunei, Singapore. |
| it | Italian | Italiano | Latin | |
| gu | Gujarati | ગુજરાતી | Gujarati | |
| am | Amharic | አማርኛ | Ethiopic | Horn of Africa. |
| kn | Kannada | ಕನ್ನಡ | Kannada | |
| pl | Polish | Polski | Latin | Three plural forms. |
| uk | Ukrainian | Українська | Cyrillic | |
| ro | Romanian | Română | Latin | |
| nl | Dutch | Nederlands | Latin | Netherlands and Belgium. |
| el | Greek | Ελληνικά | Greek | |
| hu | Hungarian | Magyar | Latin | |
| cs | Czech | Čeština | Latin | |
| sv | Swedish | Svenska | Latin | |
| bg | Bulgarian | Български | Cyrillic | |
| he | Hebrew | עברית | Hebrew | Right-to-left. |
| da | Danish | Dansk | Latin | |
| fi | Finnish | Suomi | Latin | |
| nb | Norwegian Bokmål | Norsk bokmål | Latin | Also chosen for `no` and `nn`. |

A pseudo-localization catalog (`en_XA`) ships beside these targets.

## Testing And Acceptance

Models > Mesh Editor uses a virtualized standard Qt table for keyboard component
selection, named numeric controls, scrollable Geometry/Surface/Animation/Handoff
inspectors, standard undo/save shortcuts, and text error/status messages. The
component mode includes Edges with textual endpoint, length, and face-count
columns. Split and weld controls have accessible names and explanations; welding
defaults to preserving UV/normal seams. Selected edges have dashed 3D/UV outlines
and a textual selection count, and retain their identity through undo/recovery.
Geometry's Fill Boundary Loops is a named, described, keyboard-focusable standard
button. Choose seed edges through the same component table; the tooltip explains
complete-loop expansion, displayed-pose triangulation, all-pose validation and
retained UVs/normals. New faces become the table/viewport selection for finishing.
Failures identify the boundary vertex, face or pose when available and leave
the selection/history unchanged. Work uses the existing progress/Cancel surface.
Strings use `VibeStudioModelBoundaryFill`, `VibeStudioModelDocument` and
`VibeStudioModelEditor`. The dedicated semantic fixture covers expanded labels,
RTL, both high-contrast themes, 200% text, worker locking/cancellation and the
handoff to UV projection; physical keyboard and screen-reader acceptance remain
part of the release gate.
Geometry's Bridge Boundary Loops uses the same named standard button and edge
selection table. Its Bridge twist spin box has an accessible name, description
and keyboard focus, with left-to-right numeric entry even in RTL layouts. The
tooltip explains closest-pair alignment, wrapping offsets, unequal loops and
all-pose validation. New faces remain selected for UV/normal finishing; failures
retain selection/history and use the existing worker status and Cancel action.
New bridge diagnostics use `ModelBoundaryBridge`; shared validation retains
`VibeStudioModelBoundaryFill`. The dedicated semantic fixture covers expanded
labels, RTL, both high-contrast themes, 200% text, worker locking, cancellation
and the Unwrap handoff. Physical keyboard and screen-reader acceptance remain
open release requirements.
Surface > Manage Surfaces uses named standard operation/name/target controls,
a checkable surface list and an explicit material-adoption checkbox. A plain-text
summary states the selected source, pose count, target bindings and any blocking
choice; Apply is unavailable for invalid selections. Scrollable content, wrapping
labels, mnemonics and a focusable checklist support large text and RTL. Applying
closes the review before the normal document worker exposes progress and Cancel.
New strings use `ModelSurfaceDialog`, `ModelSurfaces`, `ModelSurfacesCli` and
`VibeStudioModelEditor`. Its offscreen fixture exercises semantic controls,
expanded labels, both high-contrast themes, 200% text and actual 1x/2x pixels;
physical keyboard and screen-reader acceptance still require human testing.
Surfaces selection mode uses the existing named extended-selection table and
combo, with surface names, geometry counts and material text. The status names
the selected count and active member. Its minimum width adapts to keep names and
both counts visible at enlarged text sizes; long names and materials retain
elision, tooltips and horizontal scrolling. Table focus can change the active member
without replacing the set. Descriptions explain that the surface combo adds an
active member and that transforms use one pivot, including unused vertices.
Component-only actions are unavailable in this mode. Existing numeric controls
provide a keyboard path to the same transform as viewport handles. New shared
diagnostics use `ModelSurfaceSelection`; editor and viewport text remains
translatable. Offscreen tests cover focus semantics, scaling, expanded text and
RTL; physical keyboard and screen-reader acceptance remains a release gate.
Surface > Manage Material Slots uses a standard named single-selection list,
path field and Add/Replace/Remove/Move/Clear buttons with descriptions and
mnemonics. Apply commits the reviewed list only; a pending field edit or unsafe
path exposes a textual blocking status. The scrollable form wraps long rows;
slot paths remain available in list text and tooltips. Preview slot is a named
standard combo and does not create an undo entry. Technical indices and paths
retain left-to-right order within RTL forms. Disabled external controls on MDL
models identify the Animation skin controls. New strings use
`ModelMaterialSlots`, `ModelMaterialSlotsDialog`, `ModelMaterialSlotsCli` and
`VibeStudioModelEditor`. Dedicated offscreen fixtures exercise expanded text,
RTL, 200% text, high-contrast themes, semantic focus/accessibility metadata,
worker locking/cancellation and preview pixels. These checks do not substitute
for physical keyboard or screen-reader acceptance.

Models > Skin uses a scrollable, wrapped form with named standard combos for
surface/material slot or MDL skin/member, plus Skin File and Reset
buttons. Labels have buddies, paths and indices remain left-to-right in RTL,
and descriptions distinguish session preview from authored/exported bindings.
The read-only details area retains selectable input identity and textual image
diagnostics. Loading disables appearance controls; cancellation re-enables them
after retired work settles. The package picker retains its existing semantic
table and exact-entry selection. New text uses `ApplicationShell`,
`VibeStudioModelAppearance` and `ModelAppearanceCli`. Owned offscreen widget
tests exercise 200% text, both high-contrast themes, expanded strings, RTL,
focus metadata and exact preview pixels; physical accessibility remains open.

Vertices mode selects exact points, with square selected markers and explicit
X-ray controls for hidden vertices. The gizmo labels its axes X/Y/Z, with arrows
for movement, rings for rotation, and boxes for scale. Centre boxes provide
view-plane movement or uniform scaling. Numeric transforms and the table remain
the keyboard path; named tool, pivot, Snap, step, and view-preset controls are
standard Qt widgets. Only the selected tool's step is shown. Geometry offers
origin, selection-centre, and custom pivots, showing custom coordinates on demand.
Transform axes is a named, focusable World/Selection/Custom combo. Custom reveals
three left-to-right angle fields with accessible descriptions. The viewport names
non-world axes and reports an unavailable selection basis with recovery guidance.
Selection axes use the displayed pose for every affected frame; changing the
basis cancels a gesture. Numeric transforms, extrusion and duplication share the
same controls. Collision sizing retains its labelled intrinsic local axes.
Rotate also has a round **Free** centre and dashed circle for trackball rotation.
The shape and text distinguish it from the constrained axis rings without
relying on color. Its help explains rim projection and angular snapping; the
accessible description announces an active free preview. Existing Geometry
fields provide equivalent XYZ rotations for keyboard use, with angle snapping
off when reproducing a free preview. The starting view, pivot and axes stay
fixed; Escape or a context change cancels. Free labels share the existing
font-aware, non-overlapping annotation placement and translatable viewport
strings. Owned widget tests cover both camera types, 1x/2x device pixels, 200%
text, both high-contrast themes and expanded RTL labels; physical assistive
technology acceptance remains part of the release gate.
Numeric table cells and coordinate fields keep left-to-right mathematical order
inside RTL layouts. Preview values, invalid positions,
and cancellation guidance appear as text, and the viewport's accessible
description reports the active tool and invalid gesture state. Escape cancels.
Gizmo labels size to the current font; fixtures exercise both frame scopes,
undo, invalid-geometry rollback, and 100%/200% RTL layouts without input injection.
Tags mode selects one named attachment through the same standard table. Animation
provides named, focusable origin fields, pose scope, identity operations, pose
copying and explicit orientation reset; Geometry remains the numeric movement
and rotation path. Tags use named diamonds and dashed local axes; selected local
axes have textual labels distinct from the transform gizmo. Overlapping tag
names are placed separately, and the full identities remain in the table.
The selected name, frame and basis appear as text. Rigid-tag scale controls are
disabled. Tag selection survives undo/recovery, and playback disables pose fields.
Strings use `VibeStudioModelEditor`, `VibeStudioModelTags`, and `ModelViewport`.
The attachment fixture covers 100% dark, both 200% high-contrast themes, RTL,
expanded text, and widget-rendered evidence without controlling user input.

The assembly dialog uses a standard Qt hierarchy, named numeric fields, labelled
source/tag controls, a scrollable inspector and toolbar overflow. Tree selection
has matching geometry hatches. Manual time sampling remains available under
reduced motion; automatic playback is disabled. Worker progress/cancellation and
close handling reuse the mesh operation surface. Missing references produce a
text diagnostic and disable bake/export while leaving the recipe editable.
`ModelAssemblyDialog`, `VibeStudioModelAssembly`, `ModelAssemblyRecovery` and `ModelAssemblyCli` contain the
new localizable strings. Semantic widget tests cover source/edit/undo/save,
cancelled preparation, context changes and expanded RTL/high-contrast layouts;
physical keyboard and screen-reader acceptance remain on the modeller gate.

Assembly recovery adds a named standard checkbox, textual checkpoint status and
a standard Qt recovery list with read-only details. Restore and discard expose
their enabled state; invalid copies remain listed with a reason. Verification,
restoration and discard use cancellable background work, and discard requires a
reviewed copy plus confirmation. Recovery dialogs inherit text scale, contrast,
language and RTL settings. The complete recipe and editor context can also be
recovered through `model assembly` without graphical input.

The UV view exposes a named Graphic accessibility role, keyboard focus, island
and seam counts, rendering state, and move deltas. Dotted seam lines, dashed
selected edges, hatched faces, and square vertices supplement color. Seams draw
above ordinary UV wires, selected edges above seams, and selected markers last,
so later unselected components cannot obscure them. Overlapping selected faces
share one hatch opacity, while unselected holes remain clear. These overlays
retain logical stroke sizes at fractional and doubled device scales. Standard
toolbar actions frame all/selected UVs and expand component selection to islands;
F and Home offer view-local framing shortcuts. Surface has named numeric pivot,
offset, and grid controls as the keyboard alternative to dragging. The pivot
combo also offers Individual Islands, with an accessible description explaining
complete island selection and shared-corner splitting. Custom coordinate fields
disable in that mode. Select Islands supplies the keyboard route from partial
components; incomplete charts fail visibly without edits. The operation uses the
normal progress, cancellation and mutation locking. New shared diagnostics use
`ModelUvTransform`. Escape or
focus loss cancels a move. Chart analysis and drawing run on a worker, and picks
wait for its current result. New strings use `VibeStudioModelUv` and
`VibeStudioModelUvView` alongside the existing mesh editor contexts. The
viewport honors high contrast and reduced motion. The shared 3D renderer
(OpenGL or Vulkan) draws on a cancellable worker, with a visible and accessible rendering state;
selection waits for the displayed view to catch up. Depth-tested hatches and
hover edges supplement color without showing hidden faces through the mesh.
Recovery has a named preference, textual checkpoint status, a keyboard-focusable
copy list, source details, progress, and cancellation. Invalid payloads retain
their error in the chooser. New strings use `VibeStudioModelRecovery`,
`VibeStudioModelEditor` and `VibeStudioModelDocument` contexts. The mesh UI smoke
test exercises edits and widget renders at 100% dark and 200% high-contrast light
with RTL/expanded labels. This does not substitute for the manual keyboard and
screen-reader release audit; see [the modeller gate](MODELLER_RELEASE.md).
MD2 export is a named action. Handoff exposes labelled, keyboard-focusable skin
width/height spin boxes with accessible descriptions and an Apply button. Settings
use document undo/recovery. Export and package preparation show cancellable work
and textual precision/metadata diagnostics. Unsupported MD2 automatic placement
is disabled when the package path ends in `.md2` or `.mdl`; the path tooltip explains why.
OBJ import uses the ordinary Open/Import action, component tables, undo and
document-worker progress surface. Package OBJ and native model preview expose a focusable,
named Cancel Preview control, textual loading/error/material states and detailed
material diagnostics. Native geometry loading also prevents stale frame export
and premature editor adoption; cancellation and metadata-only formats remain
explicit states. Native decode/read diagnostics use `VibeStudioModelMesh`;
OBJ diagnostics use `VibeStudioModelObj` and include the source line.
These strings are extracted into all translation catalogues.
Filled and wireframe viewport preparation use the same background rendering
state, exposed as text and in the accessible description. Camera/pose changes
temporarily defer component picks until the displayed snapshot catches up;
navigation remains available. Rendering/error text wraps with the available
width and grows with the current font. Wireframe selection uses a thicker dashed
stroke drawn above ordinary edges, including shared edges beside unselected
faces. Logical stroke widths scale with display density and increase in both
high-visibility themes. Attachment markers stay aligned with the displayed image.
Vertex markers now share that background preparation and remain aligned too.
Visible points retain white centres/black outlines; hidden X-ray points are
hollow and dotted, while selected points are filled outlined squares. Physical
pixel alignment preserves those shape cues at fractional and integer scaling.
Selection and pose refreshes preserve table rows and compact selected ranges;
full numeric values remain available through accessibility and tooltips when
column sizing elides them. Resizable columns retain user widths across pose
changes and resize for font, style or language changes. Header labels do not
duplicate row-selection styling.
The maximum editable-grid fixture records event-loop gaps without OS input or
screen capture. It does not replace keyboard or screen-reader acceptance.
The OBJ UI fixture exercises normal and high-visibility themes, expanded text,
RTL, editor import/error preservation and the staged-package handoff without
injecting operating-system input. Human assistive-technology review remains open.

Surface's Apply .skin File, Apply Package .skin and Last Skin Import Details
actions have native focus, accessible names/descriptions and textual states.
The package picker filters `.skin` entries, preserves exact occurrence numbers,
and omits the indexed-texture operation selector. Reading and applying use the
cancellable document worker. The read-only details view reports material changes,
unused bindings and attachment markers. Strings use `VibeStudioModelEditor`,
`VibeStudioModelSkinSource`, `VibeStudioModelSkinBindings` and `ModelSkinBindingsCli`.
Automated widget checks cover both high-contrast themes, 200% text, expansion,
RTL, cancellation, undo and material refresh. Physical keyboard/screen-reader
acceptance remains open.

Quake MDL exposes named skin/member/group selectors, duration and range controls,
header fields, indexed import, palette import, and a session-only Preview Member
action. Import Package Texture opens a metadata-only native table with named
path filter, exact entry numbers, operation selector and selection status.
Unavailable entries cannot be selected; Import Skin requires a selection.
Package changes invalidate open selections. Payload reads, palette checks and
decoding run on the cancellable model worker; success and failure are textual.
The picker uses the `VibeStudioModelSkinSource` translation context.
Native preview adds named timing, seek-time and entity-phase controls,
Preview Native Timing, Seek Time and Use Clip Timing. Its status names the active
native frame, pose, skin member and paused/playing/reduced-motion state. Reduced
motion permits deterministic seeks and prevents automatic play. Clip FPS and
smoothing disable while native timing is active. All controls provide descriptions and focus metadata in a scrollable
inspector. Unsigned flags and exact float header values use left-to-right numeric
text so opening an imported model does not clamp its metadata. Work runs through
the normal cancellable document worker; failures remain textual. Native strings
use `VibeStudioModelMdl`, `VibeStudioModelEditor` and `ModelMdlCli` contexts.
Automated widget tests cover semantic edits, names/focus, both high-contrast
themes, 200% text, RTL and expanded labels; manual keyboard and screen-reader
acceptance remains part of the release gate.
The recovery chooser additionally has 100% dark and 200% high-contrast dark
RTL/expanded-label renders, with damaged-copy, cancellation, and restoration
checks through widget APIs.

Mesh Material mode has named textual status and progress, plus focusable Cancel,
Reload Images, and Details controls. Missing images use the checker and a textual
problem count. Details presents read-only source paths, dimensions, and limitations;
colour is not the sole signal. Material UI fixtures render normal scale and 200%
high-contrast light with RTL and 50% expanded labels, and verify 3D/UV updates using
widget APIs. Native screen-reader and physical keyboard acceptance remain open.

UV atlas controls are standard named, focusable spin boxes, a checkbox and push
buttons in Surface. Width, height and padding retain left-to-right numeric entry
in RTL layouts. Same as width defaults on, keeps height synchronized and disables
independent height entry; turning it off enables rectangular dimensions. Padding
is bounded by the smaller axis. Controls fit together in the scrolled inspector;
tooltips and accessible descriptions explain selection, padding and texture
effects. Unwrap/Pack are disabled without selected faces or in Tags mode. Their
document-worker dialog exposes progress and Cancel; undo restores the old mapping.
Atlas diagnostics use the `VibeStudioModelUvAtlas` translation context. Widget
fixtures cover dark and both high-contrast themes, expanded labels, RTL, actual
1x/2x rendering, rectangular package images and worker locking/cancellation;
physical keyboard and native screen-reader acceptance remain release gates.

Pack Around Unselected adds a standard named, focusable scale-policy combo and
button alongside atlas dimensions. Tooltips and accessible descriptions explain
fixed material regions, complete-island selection, scale retention and tile
limits. Both controls disable without eligible faces and during worker edits.
The existing progress/Cancel and undo flow applies. New strings and diagnostics
use `ModelUvObstacles`; expanded strings, RTL and both high-contrast themes are
covered by owned-widget fixtures at 1x/2x. No persistent accessibility preference
or first-run setting is added.

Repair Import uses a standard named table with values formatted on demand, a
labelled pose control, a prepared-copy viewport and an explicit new destination.
The report identifies exact source indices and normal fallback choices in text;
selecting a pose-specific row updates the preview. Long summaries wrap, paths
retain left-to-right direction in RTL layouts, and Cancel is the default action.
Preparation and saving use the document worker's progress and cancellation.
Strings use `VibeStudioModelImportRepair` and `ModelImportRepairCli`. Native
keyboard and assistive-technology acceptance remain part of the release gate.

The mesh Health inspector uses standard focusable Inspect, category, Select
Findings and repair controls with names and descriptions. Findings have textual
counts and component highlights; colour is not the only signal. Stale reports
disable selection and repair, and Tags mode disables geometry health controls.
Wrapped descriptions state the complete-surface scope and limitations. Inspection
and repair share the existing cancellable document worker. Strings use the
`VibeStudioModelEditor` and `VibeStudioModelDocument` contexts. Health fixtures
exercise public Qt controls and render dark/high-contrast, 100%/200%, RTL and
expanded labels; native assistive-technology and physical-keyboard checks remain open.

The Nonmanifold Edges category exposes **Split Nonmanifold Edges** through the
same labelled button and keyboard focus order. Its wrapped description explains
the whole-surface scope, preserved two-face connections and possible new open
boundaries. A completed repair updates textual counts and expanded edge
selection; cancellation leaves the source, selection and history unchanged.
The action, descriptions and failure messages remain translatable. Widget-owned
before/after renders cover the longer label at both device scales, dark and
high-contrast themes, expanded translations and RTL. Large animated repairs use
the existing progress/cancel worker; they do not require physical input automation.

Geometry intersections use named, focusable pose scope, Inspect, face-pair and
Show First/Second Face controls within Health. Counts, contact types, zero-based
indices and wrapped surface names identify findings without relying on colour.
Choosing a face pauses playback and synchronizes the exact pose, surface,
component table and preview. Reports become stale after edits or changes to a
single scanned pose, including playback; stale navigation is disabled. The
finder formats bounded results on demand and shares the cancellable document
worker. New UI and diagnostics use `VibeStudioModelEditor`,
`VibeStudioModelIntersections` and `ModelIntersectionsCli` translation contexts.
Offscreen checks cover both device scales, 200% text, expanded labels, both
high-contrast themes, RTL, focus and accessibility metadata. Physical assistive
technology and keyboard acceptance remain separate release requirements.

Animation baking uses a scrollable standard Qt review with named clip, start,
frame-count and fractional FPS controls, numeric left-to-right entry and a
word-wrapped sample/omission summary. Invalid end times disable publication;
long sampling uses the existing progress/cancel worker. Reduced motion retains
manual baking and explicit preview seeks without starting playback. The Mesh
Editor exposes Saved clip FPS / Apply Clip FPS with undoable source timing.

The Animation clips form uses named, focusable clip/range, preview-rate, pose-copy
and insertion controls, plus a named Smooth preview checkbox. Indices distinguish imported duplicate clip names;
numeric fields retain LTR direction in RTL layouts. Text status describes the
preview, and tooltips explain source-only clip metadata, full-model pose scope,
insertion boundaries and export limits. Reduced motion disables automatic
playback in both smooth and stored-frame modes; clip and pose mutation is disabled
during playback. Pausing snaps to an exact stored pose and restores editing
handles. Incompatible surface poses and visible attachment previews have textual
and accessible status. Hidden attachments skip interpolation diagnostics.
Fraction-only updates do not emit whole-frame editor
refresh signals. The smooth option and preview rate are session-only. Operations share
the cancellable document worker. Strings use `VibeStudioModelAnimation`,
`VibeStudioModelEditor` and `VibeStudioModelDocument`. The animation UI fixture
checks public controls and renders dark/both high-contrast themes, 100%/200%,
RTL and expanded labels; physical keyboard and native screen-reader acceptance
remain release work.

Mesh document operations use a named, window-modal progress dialog with textual
phase, standard progress bar, and keyboard-focusable Cancel button. Escape and
close request cancellation while the dialog remains visible until completion.
Mutating actions and their shortcuts are disabled for the duration. Widget tests
exercise cancellation and editor close through APIs; progress renders cover
normal text and 200% text with high contrast, RTL, and expanded translations.
The studio's deferred mesh close resumes after the worker and unsaved-changes
decision; Cancel withdraws the pending close instead of requiring users to
track a delayed action. The existing standard Qt prompt remains the decision
surface, and move previews cancel before the close decision.
These do not replace native keyboard or assistive-technology acceptance.

The Models > Design Prop dialog uses standard named Qt controls, a focusable
parts list, Undo/Redo actions, an overflowable command toolbar, and a scrollable
Part, Surface, and Handoff property tabs. Vector components occupy separate rows so enlarged text and RTL
layouts retain usable controls. Errors and handoff results use text, not color
alone; the static preview inherits high-contrast colors and has no playback.
The parts list and preview select the same part; a hatch and a text readout
identify the selection. Rotation and UV controls are named standard spin boxes,
and preview rendering is a named combo. UV reset is one undo step. The parts
list and property controls provide a keyboard path to every modelling edit.
Shared theme control minimums now include the scaled font height, so scrollable
forms across modules cannot compress enlarged text into the unscaled minimum.
All labels and messages use the `VibeStudioModelDesignDialog` or
`VibeStudioModelDesign` translation context. `model-design-ui-smoke` checks
100% dark and 200% high-contrast RTL layouts with expanded labels, control
metadata, transform/UV edits, selection, saved-state tracking, undo/redo,
validation, package/map handoff, and the real shell's dependency scan without
injecting input. Keyboard-only and screen-reader operation still require the
manual checks below.

The Levels dependency browser uses named standard Qt widgets, text status labels,
resizable columns, a searchable object/reference list, a Problems only filter,
and a read-only details pane. Row activation and the Select in Map button share
object navigation; Cancel Scan and Cancel Export expose interruption explicitly.
All new strings enter Qt Linguist catalogs. `level-dependency-ui-smoke` checks
focusability, accessible names, filtering, details, and export enablement at
100% dark and 200% high-contrast dark with RTL layout. It changes widget state
directly without injecting keyboard or mouse input. Actual screen-reader and
keyboard-only operation remain part of the manual acceptance checklist.

- [ ] Run layout smoke tests at 100%, 125%, 150%, 175%, and 200% scale.
- [ ] Run high-contrast dark and high-contrast light smoke tests.
- [ ] Run keyboard-only setup and package/compiler workflow smoke tests.
- [ ] Run screen-reader metadata spot checks for shell, setup, preferences,
  package tree, activity center, compiler log, and editor profile controls.
- [ ] Run TTS smoke tests for enabled event categories.
- [x] Run pseudo-localization and right-to-left layout checks in CI or release
  validation.
- [x] Run pluralization and translation expansion layout smoke checks in CI or
  release validation.
- [x] Run a Qt Linguist extraction dry-run in local and CI validation.
- [ ] Track untranslated strings and stale translations as release blockers once
  a language is marked supported.

## WAD group edit review

WAD Groups uses labelled group/operation selectors, a labelled map-name field,
a paged exact-change table, readable status/details and separate Review/Apply
controls. Work runs asynchronously with Cancel Operation; Close remains outside
the scrolling body and joins cancellation before destruction. Screen-reader row
text identifies the source occurrence or new entry and before/after names. New
WADs use the same review without an intermediate save/reopen. Labels wrap and
content scrolls at 200% with RTL/expanded text; table cells preserve full tooltips.
Strings use PackageWadGroupsDialog, VibeStudioPackageWadGroups and the shared
VibeStudioPackageSubset scan context. Native assistive-technology acceptance
remains part of the package release audit.

Doom launch preparation uses a window-modal Qt worker dialog with named status,
progress and Cancel controls. Status labels wrap, Cancel participates in normal
tab focus, and reduced motion replaces the indeterminate animation with a static
progress track. Closing requests cancellation and waits for worker acknowledgement.
`level-doom-nodes-ui-smoke` renders 100% dark and 200% high-contrast RTL layouts
with expanded translations and checks responsiveness while validating a 48 MiB
WAD. Strings use `GameLaunchTaskDialog` and `LevelDoomNodes`. Native keyboard and
assistive-technology acceptance remain manual checks.


UDMF Properties uses standard named Qt controls, labelled property/object fields,
a table with accessible headers, visible textual change states and wrapping
status. Validation runs on a cancellable worker, uses a static progress track
with reduced motion, and acknowledges cancellation before closing. The shell
retains the existing text scale, theme and RTL settings. `level-udmf-ui-smoke`
checks focusability, accessible interfaces, Apply/Cancel/error paths and widget
renders at 100% dark and 200% high-contrast with expanded RTL translations.
Columns reserve scaled room for property labels. No native keyboard input or
OS capture is used; actual screen-reader/keyboard acceptance remains unverified.
Strings use `LevelUdmf`, `LevelUdmfDialog` and the existing shell/node contexts.

UDMF standard transforms use the same named actions and cancellable placement
progress controls as other map formats. Numeric rotation uses labelled LTR
numeric controls within RTL layouts, the shared Models preview and a static
progress track with reduced motion. Long translated status tokens wrap without
widening the controls column. `level-udmf-transform-ui-smoke` checks actual shell
move, turn, mirror, connected selection, resize and numeric rotation, exact undo,
save, accessible control roles/focus metadata and 100%/200% high-contrast RTL
renders. Native keyboard and screen-reader acceptance is still required.

UDMF thing duplication uses the same named **Duplicate Selection** and
**Duplicate with Offset…** actions. The placement dialog exposes six decimal
places on XYZ controls and enables height for UDMF, while retaining whole-unit
controls for binary Doom/Hexen. Shared preview, count, progress and cancellation
controls retain their accessible names, roles and focus behaviour. The UDMF
UI suite also exercises these controls at 100% and 200% high-contrast RTL,
confirms fractional-height previews and verifies exact shell duplication undo.

## Workspace and asset-format cohesion

The Level texture-check status keeps Qt's native text-derived accessible name,
so loading, cancellation, completion and incomplete results are exposed with
normal label change notifications. A refused planned view also exposes its
reason through the accessible description.


The Levels Health texture check exposes a wrapping status, named native progress,
Cancel and Retry controls. Reduced motion uses a static initial progress state.
The audit runs off the GUI thread against the same planned package as the
browser; package edits and Undo/Redo clear obsolete results. Incomplete checks
and unavailable planned views use explicit text and retain diagnostic rows,
without a success claim based on partial counts. Workspace package details
refresh while retaining the selected section. These controls use existing text
scale, theme, layout direction and translation preferences. Native keyboard and
screen-reader acceptance remains required.


Multitrack Sessions supplies a named track/clip tree, standard numeric inspectors,
automation tables, frame controls, operation status/progress and Cancel Operation.
Its custom timeline exposes a Graphic role, strong focus and current frame/selection
description. Tree/inspector controls provide exact editing without dragging;
Left/Right nudges a focused clip by the selected snap step. The time axis stays
left-to-right within RTL layouts. Editing controls scroll on short/scaled windows;
operation status and cancellation remain outside that scrolling body. Themes use
palette roles and textual mute/solo state, with an outline for muted clips.
Reduced motion uses static progress during indeterminate work. Strings use
`AudioSessionDialog`, `AudioSessionTimeline`, `AudioSession`, `AudioSessionCli`
and the existing shell/editor contexts. The offscreen session suite checks
accessible roles/names and 100/125/200% dark/light/expanded RTL widget renders.
Output selection, refresh and buffer size use named standard controls. The cursor
field seeks streamed playback without dragging. Transport state includes textual
frame, buffer, dropout and stereo peak information; color is not required to
identify failures or clipping. Preparing playback respects reduced motion and
can be cancelled. New strings use `AudioTransport`, `AudioSessionPlayback` and
`AudioStreamDevice` alongside the session contexts. Native keyboard, screen-reader
and physical audio-device acceptance remain open.

Session recovery uses the same translatable checkbox, startup offer and review
manager as waveform recovery. The manager identifies Waveform/Session in text;
session rows include track/clip counts, while editor-lease presence is distinct
from content verification. Checkpoint status exposes its text as an accessible
description, and the preferences/review controls remain inside the scrollable
session body. Restore verifies the selected digest and opens the matching editor
as an unsaved draft; it does not start playback or change focus at startup.

Open Workspace and Save Workspace As use accessible File-menu and command-palette
actions. Native file dialogs retain keyboard and screen-reader behavior.
Workspace errors, missing-reference diagnostics and format limitations are
translatable. Texture import filters derive from the common catalog, including
available Qt codecs. `workspace-ui-smoke` exercises actions and restored module
context with expanded translations, high contrast and 150% text through Qt
service calls, without operating-system input injection. Workspace files store
stable module IDs and paths rather than translated labels.


Audio stem delivery uses a native checkable list and labeled frame, precision,
signal-point, dither, seed, prefix and directory controls. Export remains disabled
for invalid selections; a filename preview and textual status expose the plan.
The scrollable form wraps expanded labels, inherits theme/scale/RTL and provides
keyboard focus and native accessible roles for every control. Technical filenames
and the optional delivery report retain left-to-right presentation. The normal
worker supplies cancellable progress, and completion distinguishes committed,
failed and pending files. UI strings are extracted into all target catalogs;
native screen-reader and physical-device acceptance remain separate gates.

The Audio effects inspector reports insert latency as text. Its labeled lookahead
spin box exposes a tooltip and accessible description explaining that changing
lookahead rebuilds compensation and cannot be automated. The automation selector
offers ceiling, attack and release only for this processor. Normal staged edits,
undo, scalable scroll layouts, high-visibility themes and RTL remain shared with
the other effects. Native screen-reader/device acceptance remains a separate gate.

## Quake III Native Animation

The assembly Native Animation dialog uses standard Qt tree, combo and numeric controls with accessible names, explicit model ranges, validation text and a scrollable form. It retains keyboard focus paths, translated labels, RTL layouts, high-visibility themes and text scaling. Reduced-motion assembly preview supports exact manual times. Widget-level verification does not replace physical keyboard or screen-reader acceptance. See [Native Animation](MODEL_ASSEMBLY.md#quake-iii-native-animation).

Assembly linked-skin controls use the existing scrollable inspector and standard
Qt combo, path, occurrence and button controls with accessible names/descriptions.
Mode switches enable applicable inputs; path/numeric fields retain left-to-right
direction under RTL. Input resolution uses visible cancellable work and reports
missing skins without retaining a stale preview. Details exposes before/after
materials and input identities as selectable text. Reduced-motion sampling,
themes and text scaling follow the assembly editor. See
[Linked Skins](MODEL_ASSEMBLY.md#linked-skins).

## Native Player Package Review

Assembly **Player Package…** uses a scrollable Qt form with wrapping labels,
accessible names/descriptions, normal focus navigation and logical LTR identifiers,
paths and entry numbers inside RTL layouts. File rows expose SHA-256 details to
accessibility APIs and to the wrapped Details pane on keyboard selection. Elided
paths retain both ends. Wrapped status and notes communicate review/publication state.
Editing options invalidates the review and disables export. Preparation and writes
use the existing cancellable worker/progress surface. The image picker uses exact
package metadata and has no mesh-edit controls. Dark, both high-contrast themes,
200% text, expanded labels and device scaling are widget-test requirements;
physical keyboard and screen-reader acceptance remain separate release work.
See [native player packages](MODEL_ASSEMBLY.md#native-player-packages).

## Placed Model Appearance Diagnostics

Per-instance compiler skins and remaps use the existing keyboard-focusable
Levels entity inspector and Camera Details control. Loading, omitted surfaces,
unavailable appearances and the static-pose remedy are textual states; colors
are not the only signal. Details remain selectable, wrapped, read-only text
with map selectors and exact source paths/hashes. New diagnostics are extracted
through the `VibeStudioLevelModelAppearance` and `VibeStudioLevelMaterials`
translation contexts. The dedicated Qt UI test exercises inspector edits,
history, high contrast, RTL and expanded translations; owned renders are checked
at actual 1x and 2x display scale. Native platform/screen-reader acceptance
remains part of the release gate. See [the workflow](LEVEL_MODEL_APPEARANCE.md).

## Modeller Profiles And Layout

The Mesh Editor's outliner and property pages sit on the shared `StudioSidebar`,
so they keep its keyboard order, accessible tab names (also for icon-only tabs),
folding and right-to-left mirroring. Each of the four view panes has an
accessible name naming its view ("Top view"), shows that name in its corner, and
marks the active pane with a border as well as its accent colour. Controls
profiles change keys and gestures only: every command keeps its menu entry,
command search (<kbd>F3</kbd> or the profile's key) and keyboard route, and
**Customise Controls…** lists every binding in labelled tables, reports
collisions in text, and offers **Reset**. The status line keeps its height
during a viewport drag, so wrapped translated text cannot cancel the gesture.
Profile, sidebar and format strings use the `VibeStudioModelControls`,
`VibeStudioModelSidebar`, `VibeStudioModelSkeleton` and `VibeStudioModelMesh`
contexts. `model-profiles-ui-smoke` runs each profile at 100% dark and at 200%
high-contrast light right to left. Real-device keyboard and screen-reader review
remains part of the [modeller gate](MODELLER_RELEASE.md).
