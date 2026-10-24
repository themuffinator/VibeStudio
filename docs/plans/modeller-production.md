# Modeller production plan

The goal is a complete modeller for idTech game work. VibeStudio remains
pre-alpha and largely untested in real projects. Functionality and acceptance
are separate; the [release gate](../MODELLER_RELEASE.md) remains the authority
for completion. Synthetic fixtures do not establish engine or real-project
acceptance.

## Workflow coverage

| Area | Existing foundation | Remaining work and acceptance |
| --- | --- | --- |
| Documents | Mesh/design/assembly sources, undo/redo, guarded saves, recovery, workers | Lossless edits across retained source features; interruption, maximum-size and competing-writer evidence |
| Mesh construction | Primitives, component/surface selection, transforms, extrusion, inset, loop cuts, [edge sliding](../MODEL_EDGE_SLIDE.md), [edge bevel](../MODEL_EDGE_BEVEL.md), [knife paths](../MODEL_KNIFE.md), [spin and radial copies](../MODEL_SPIN.md), [rip and fill](../MODEL_RIP.md), [persistent connectivity](model-topology-identity.md), bridge/fill, merge, repair, decimation, [exact skin-weight provenance](../MODEL_SKIN_PROVENANCE.md) through every edit | Richer bevel/rip junctions, symmetry and constraints; complete low-poly modelling loops. |
| UVs and materials | Projection, seams, islands, transforms, atlas/obstacle packing, [texel density](../MODEL_UV_DENSITY.md), skins and slots | Texture handoff, unwrap quality, multi-material density matching, painting/baking and preview parity |
| Vertex animation | Frames/clips, interpolation, timeline, MDL groups, MD2/MD3 export | Real model editing and engine playback; target limits and metadata |
| Rigging | Skeleton retention, MD5/IQM export; [joint, numeric weight, bind/pose and dense clip authoring, bone selection and viewport posing](../MODEL_RIG.md); [influence inspection, explicit binding and repair](../MODEL_SKINNING.md); [staged weight painting](../MODEL_WEIGHT_PAINT.md); [mirrored/spatial transfer](../MODEL_WEIGHT_TRANSFER.md); [two-bone IK](../MODEL_IK.md); [persistent TRS curves, batch key tools and range transfer/reversal](../MODEL_ANIMATION_CURVES.md) | Curve clipboard/layers, persistent constraints, long-chain IK, cross-document retargeting and animation fidelity |
| Attachments and assemblies | Tags, linked assemblies/skins, Quake III animation configuration/player bundles | Native variants, LOD workflow, sounds and attachment behaviour in engines |
| Collision and LOD | Static/animated boxes, map/level handoff, MD3 LOD export | Convex collision where supported, linked refresh and target acceptance |
| Native formats | See [format matrix](../MODEL_FORMATS.md); MD5/IQM/ASE exports write or report every omission, strict exports refuse lossy writes, and [delivery targets](../MODEL_TARGETS.md) preflight vanilla and source-port limits | Independent files/engines, malformed inputs, retained metadata, **Prepare for target** fix-its, saved per-project targets and RTCW/ET, GZDoom and Q2PRO MD3/MD5 targets |
| Studio integration | Shared paths, previews, materials, dependencies, staging, levels and compilers | Source-to-package-to-game loops, stale derivatives and refresh ownership |
| CLI | Shared services with text/JSON output; complete dry-run source-save validation and loose/package/draft input hard-link protection | Maintain parity, determinism, cancellation, diagnostics and safe outputs; broader filesystem and concurrent-writer acceptance |
| Accessibility | Qt controls, translation, scalable/high-contrast themes, profiles; [trap-free keyboard focus](../MODEL_TOOLS.md#keyboard-focus), profile-driven key hints and keyboard routes for loops, rings, Loop Cut, the UV view and view panes | Physical keyboard/screen-reader review, 200% text crowding, gizmo accessible objects, RTL/expansion, reduced motion and high DPI |
| Release | Automated unit/integration/render/performance harnesses | Clean Windows/macOS/Linux packages, real-project evidence and bounded performance |

## Work order

1. Close skeletal authoring gaps through existing document/worker services.
   Numeric joint/weight, dense pose/clip editing and shared viewport handles
   are implemented alongside influence inspection, explicit binding and weight
   repair, staged weight painting, mirrored/spatial transfer, two-bone IK and
   persistent curves with batch key tools and range transfer/reversal. Persistent constraints,
   long-chain IK, retargeting and more complete
   animation controls remain.
2. Audit mesh/UV tools against complete low-poly production tasks, adding
   missing operations with shared GUI/CLI semantics.
3. Validate delivery for Quake MDL, Quake II MD2, Quake III MD3 and supported
   skeletal/source-port targets, including animation, attachments and collision.
4. Complete accessibility, performance, resilience and clean cross-platform
   packaging acceptance on identified source/build inputs.

## Evidence discipline

Delivery targets, detail levels, skin variants and Quake III player packages
(2026-10-23; FMT-06, ATT-LOD-02, UVSKIN-10, ATT-PLAYER-01 to 03). **Prepare for
Target** and `model prepare` apply the preflight's split, join, decimate, fit
and influence fixes as one undo step; targets and the strict setting are saved
per project or suggested by the game installation, and new targets cover RTCW,
Enemy Territory, Q2PRO and GZDoom ([delivery targets](../MODEL_TARGETS.md)).
Detail levels live in the source as a recipe (version 13, [detail
levels](../MODEL_LOD.md)), are checked the way Quake III reads a set and are
exported or staged with the model; skin variants (version 14, [skin
variants](../MODEL_SKIN_VARIANTS.md)) write `.skin` files or Doom 3 skin
declarations. **Player Package** adds team skins, icons, detail levels and the
custom sounds, and **Open Player…** opens an existing Quake III player as an
assembly. A packed source now takes the version of its newest feature and is
recognised by its packedArrays field. The new model-target-prepare, model-lod,
model-skin-variants and model-player-import suites (core and GUI) pass with the
extended targets and player-bundle suites; the lane's full modeller run passed
158 of 160, both failures outside the lane. Nothing has engine or real-project
acceptance yet.

Mesh Editor at 200% text, screen-reader announcements, high contrast, reduced
motion and localisation (2026-10-23; A11Y-2, A11Y-3, A11Y-5, A11Y-6, A11Y-9).
The editor's document commands are a **Model** menu, one eliding status strip
replaces the wrapping rows (in Material shading it also carries the material
images' state, with **Cancel** while they load and an **Images** menu), sidebars
wrap and fold to keep the views at least 45% of the width, and modeller dialogs
open within the screen. Status lines carry their text in accessible
descriptions; results, long-task ends and problems are announced, and read aloud
with speech on, while previews and readouts stay quiet. Every modeller preview
follows high contrast and reduced motion. More than 40 count messages use plural
forms and display numbers follow the locale, while Copy, files, the MDL header
fields and the CLI keep the C locale. model-editor-layout-ui-smoke,
model-accessibility-ui-smoke and model-locale-ui-smoke are new; the lane's full
modeller run passed 154 of 155, the failure being a fixture main had already
fixed. Everything is measured offscreen; no screen reader or real device has
been tried.

Skeletal working set, source size and undo accounting (2026-10-22). Skeletal
models over 1,024 frames or 1,048,576 frame vertices are re-baked into a working
set of clips instead of refused; every clip stays in the skeleton, the working
set changes through one undoable edit ([skeletal working
set](../MODEL_SKELETAL_WORKING_SET.md)), and the skeletal tools edit and preview
clips outside it. Sources write shortest exact floats and pack bulk arrays
(version 12) when readable JSON would pass 64 MiB, so the realistic grid saves
in 34.7 MiB where it failed at about 122 MiB. Undo history counts skeletal data
and shared arrays once, keeps every redo step and has a 64 MiB **Undo history
limit**. model-skeletal-working-set-smoke (94 checks) and its GUI suite (91),
model-source-budget-smoke and model-history-budget-smoke with its GUI suite
pass; the lane's full modeller suite passed 156 of 157, the failure being a test
fixture already fixed in main. Clips longer than the whole budget cannot play in
the main viewport, and skeletal weight edits keep only about two undo steps on
large rigs until frames are derived on demand.

Mesh Editor keyboard access (2026-10-22) removes the Tab keyboard trap in
every profile: Ctrl+Tab and Ctrl+Shift+Tab (Control+Tab on macOS) move
between panels and cannot be bound, Tab-bound commands work only in the view,
tool bars are single Tab stops and F9/Esc enter and leave **Adjust Last
Operation**. Key hints follow the active profile. Edge loops and rings of the
selection, Loop Cut, UV pan/zoom and view switching have keyboard routes, and
the navigation gizmo keeps clear of the HUD at 200% text and RTL. The new
model-editor-keyboard-ui-smoke uses real key presses in four profiles at 100%
LTR and 200% RTL; mutation checks show it catches the missing key claims. The
full modeller suite passed 144 of 144 in its lane. No physical keyboard,
screen reader or macOS run has been made, and the gizmo has no accessible
objects.

Lossless skeletal export (2026-10-22) makes MD5, IQM and ASE exports write or
report every omission: md5mesh weights carry joint bind scale and mirroring,
md5anim refuses poses Doom 3 cannot follow and needs a whole frame rate, and
IQM reports reduced influences, rounding and layout loss. MDL, MD2 and MD3
keep clips their frame names rebuild. `--strict` and **Refuse exports that
leave data out** refuse lossy writes. Thirteen [delivery targets](../MODEL_TARGETS.md)
preflight vanilla and source-port limits from public engine sources, Quake III
detail sets are capped at the two levels the game loads, and an independent
Python oracle reads MD5, IQM and ASE without VibeStudio's decoders. The full
modeller suite passed 145 of 146 in its lane (the known assembly playback
timing check, since fixed). **Prepare for target**, saved targets, RTCW/ET,
GZDoom and Q2PRO targets, and engine load tests remain open.

Skin-weight provenance (2026-10-22) replaces the nearest-vertex skin
reconciliation. While an edit runs, every vertex records where it came from
(`core/model_vertex_origins`); unchanged and reordered vertices keep their
influences bit for bit, cuts interpolate them, Symmetrize maps them through a
joint mirror map, moved copies are rebound and posed from the skeleton, merges
blend equally and new primitives need an explicit binding. Edits that cannot
derive a skinned vertex's weights are refused, and frame edits keep the
frame-to-clip map. model-skin-provenance-smoke (326 checks), its sweep over
every edit kind (1,185) and its GUI suite (132) pass on fixtures where a
nearest-vertex guess fails; mutants of each review fix are caught. Merges are
an equal blend, position tools still make the frames they edit independent,
and widened blends are reported rather than pruned. See
[skin provenance](../MODEL_SKIN_PROVENANCE.md).

Persistent connectivity passed 60 unique suites and three 2x GUI reruns, with
494 scoped source/build inputs and 61 executables stable throughout the final
interval. Source version 11 preserves coincident separation through mesh, UV,
surface, history, recovery and assembly workflows; ordinary and hold-next
skeletal curves coexist with it. The new suite passed 416 checks. Rip passed
133 core/CLI and 183 GUI checks per scale; 24 owned renders and four final
visual reviews cover the user workflow. Evidence is in
`.agents/tmp/modeller-topology-identity/evidence/milestone.json`.
The skin-weight provenance milestone above closes the nearest-vertex
reconciliation that this checkpoint left open. Native game formats generally
lack authoring connectivity identities.
The full production goal remains active.

Rip and Rip and Fill passed fifty-seven focused suites and three GUI reruns
at 2x device pixels, sixty executions total. New coverage includes 131
core/CLI checks and 153 GUI checks per scale. All 491 scoped source/build
inputs and 58 executables remained stable during acceptance. Evidence in
`.agents/tmp/modeller-rip/evidence/milestone.json` records topology/skin/UV
preservation, capacity and cancellation, native exports, recovery, F9/undo,
profile commands, eighteen owned renders and that checkpoint's limits. The
persistent-connectivity work above supersedes its nonzero-offset restriction.
Richer junctions, physical accessibility,
maximum-size latency, real projects/engines and cross-platform acceptance
remain open; the production goal is active.

The first joint/weight increment passed fourteen focused regression suites and
a separate 2x GUI run (68 new core/CLI checks, 70 new GUI checks per scale).
Its evidence is recorded in [the release audit](../MODELLER_RELEASE.md).
Bind/pose transforms and skeletal clip authoring passed eighteen focused suites
and a separate 2x GUI run (101 core/CLI checks and 81 GUI checks per scale),
with source/executable hashes and eighteen reviewed widget renders in the
[release audit](../MODELLER_RELEASE.md).
Joint viewport tools passed thirty suite executions: twenty-six functional
suites, three existing performance suites and the new GUI suite at actual 2x
device pixels. The new suites supply 61 core/CLI checks and 135 GUI checks per
scale, with twenty-six reviewed widget renders. The performance fixtures cover
existing dense mesh operations; maximum-size skeletal dragging still needs
separate acceptance. Influence inspection/selection and explicit binding are now
implemented; staged painting is also implemented. Richer animation controls remain. The full goal remains open.

Influence inspection, binding and repair passed twenty-seven regression suites
and the new GUI suite at 1x/2x (93 core/CLI checks and 92 GUI checks per scale).
Twenty widget renders were reviewed. The [release audit](../MODELLER_RELEASE.md)
records source/build identity and the remaining real-project, performance,
engine and platform acceptance requirements.

Staged weight painting passed thirty focused suites and a separate 2x GUI run
(78 core/CLI checks and 164 GUI checks per scale). Twenty-six widget renders
were generated, with representative visual review at both scales and automated
geometry/label-height checks. The [release audit](../MODELLER_RELEASE.md) records
the accepted results, stable source/executable identity and remaining mirrored
transfer, animation, performance, engine, accessibility and platform gates.

Mirrored/spatial weight transfer passed thirty-two focused suites and a separate
2x GUI run (94 core/CLI checks and 161 GUI checks per scale). Thirty widget
renders were generated and representative states reviewed at both scales.
The dense correspondence fixture covers 16,391 vertices; maximum-size latency
still needs acceptance. The [release audit](../MODELLER_RELEASE.md) records
source/build identity, the repaired RTL viewport statistics and remaining
retargeting, animation, engine, accessibility and cross-platform gates.

Two-bone IK passed thirty-three focused suites and a separate 2x GUI run
(80 core/CLI checks and 129 GUI checks at each scale). Thirty widget renders
were generated and representative states reviewed. The 1,023-sample timing
fixture uses only six vertices and seven joints. The [release audit](../MODELLER_RELEASE.md)
records source/build identity and the remaining curves, constraints, long-chain
IK, retargeting, engine, accessibility, real-project and platform gates.

Persistent TRS curves passed thirty-five focused suites and a separate 2x GUI
run (89 core/CLI checks and 124 GUI checks at each scale). Twenty-four widget
renders cover numeric keys, the curve graph/tangents and posed previews.
Near-gimbal capture, parent/child curves, source schema 9, explicit baking and
native export are covered. The 1,023-sample timing fixture remains small.
See the [release audit](../MODELLER_RELEASE.md) for evidence and outstanding
constraints, retargeting, real-project and platform gates.

Batch animation key tools passed thirty-seven focused suites and a separate
2x GUI run (72 core/CLI checks and 146 GUI checks at each scale). Thirty widget
renders cover selection, operation settings, continuous-change reports and
original/result poses. Retiming, duplication, removal and reduction share one
document transaction and combined clip rebake. The 50,001-key reduction timing
fixture covers one scalar linear curve, not maximum-size animated meshes.
All 444 scoped inputs were stable across those accepted runs. The
[release audit](../MODELLER_RELEASE.md) records source/build identity and the
remaining animation, mesh/UV, engine, real-project and platform gates.

Range transfer/reversal
passed thirty-nine focused suites and both transfer/curve-editor GUI suites at
2x (72 new core/CLI checks and 124 new GUI checks per scale). Fifty new
renders cover selection, ranges, reports, previews, exact step graph jumps and
900 × 600 logical layouts with 200% text and RTL. The 1,023-sample, seven-joint
fixture has only six vertices. All 454 scoped inputs and 40 executables remained
stable across final acceptance. The release audit records the tested identities,
an unrelated Qt macro build repair in the concurrent level-editor code and the
remaining production gates.

Edge bevels passed forty-two focused suites and three additional GUI runs at
2x: forty-five executions, with 252 new core/CLI checks and 147 new GUI checks
at each scale. Twenty-four new renders cover pointer, typed, adjustment and
refusal states. The 128-cube, two-pose fixture expanded 3,072 vertices to 31,744
in 132 ms; maximum-size performance remains unverified. All 462 scoped inputs
and 43 executables remained stable across acceptance. The
[release audit](../MODELLER_RELEASE.md) records source/build identity, native
round trips, accessible compact status and the remaining production gates.

Knife paths passed forty-four
focused suites and four additional 2x GUI runs: forty-eight executions, with
202 new core/CLI checks and 312 GUI checks per scale. Forty-two new renders
cover paths, visibility, perspective, adjustment and refusal. The synthetic
thirty-segment, two-pose plane produced 289 vertices in 170 ms. All 468 scoped
inputs and 45 executables remained stable during acceptance; source protection,
selection publication and enlarged adjustment labels also received shared
fixes. See the [release audit](../MODELLER_RELEASE.md) for limits and evidence.

Texel-density inspection and adjustment passed fifty-seven focused suites and
five additional GUI runs at 2x: sixty-two executions, with 92 new core/CLI checks
and 242 GUI checks per scale. Thirty-six owned renders cover reports, UV
comparison, and 900 × 600/500 × 600 logical layouts with expanded RTL text.
The real package-material path supplies original dimensions. Exact skin copying
and animation-transfer source protection received related fixes. A synthetic
2,048-island, 8,192-vertex, two-pose fixture exercises batched scaling; maximum-size
latency remains open. All 476 scoped inputs and 58 executables were stable across
acceptance. The [release audit](../MODELLER_RELEASE.md) records evidence and the
remaining material, mesh, animation, CLI output-policy, engine and platform gates.

The source/output audit passed forty-four focused suites with stable identities
for 478 scoped inputs and 45 executables. The new save suite supplies 88 checks;
four auxiliary-input suites cover 24 direct/path-alias/hard-link refusal cases
across dry and real writes. Complete serialisation, changed/removed sources,
cancellation, ordinary in-place edits, native output and undo/history are covered.
That milestone identified a separate package provenance gap, addressed by the
following shared-service increment. See the [release audit](../MODELLER_RELEASE.md).

Package hard-link protection passed sixty-two package/modeller suites with stable
identities for 661 scoped inputs and 63 executables. Forty-five new identity
checks cover native and portable comparisons, folder descendants, limits,
cancellation, cache reuse, draft provenance, publication and extraction. Real
modeller CLI cases preserve archive/folder/draft aliases; frame exports cancel
during folder capture, and player outputs protect backup and lock aliases too.
Native macOS/Linux, network/ReFS filesystems, concurrent filesystem mutation,
maximum-size responsiveness and the broader production gates remain open.

Spin and radial copies passed fifty-five focused suites and three GUI runs at
2x: fifty-eight executions, with 100 new core/CLI checks and 174 GUI checks per
scale. Eighteen new renders cover previews, copies and scrolling adjustments.
Skeletal reference poses, independent frames, native export, cancellation,
capacity, recovery and component selection are covered. All 482 scoped inputs
and 56 executables were stable across acceptance. A separate repository layout
check reports a missing Meson install entry for the concurrently added level
textured-selection header. See the [release audit](../MODELLER_RELEASE.md) for
evidence and the remaining rip, mesh/UV, animation, engine, platform and
physical accessibility gates.

Keep logs and owned render targets under `.agents/tmp/`; identify tested
sources, executables, platform, renderer and limitations. Do not imply that
tests cover later shared-checkout changes. Engine validation uses the engine's
registered screenshot command and windowed launches. Qt tests use owned widget
APIs and render targets without controlling the user's keyboard or mouse.
Production readiness requires every release gate, not just implementation.
