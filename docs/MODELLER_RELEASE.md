# Modeller Release Candidate Gate

The target is a professional modeller for idTech game production. The static
prop designer and model browser are useful foundations, but do not satisfy this
gate. A checked implementation milestone is not a release approval. Evidence
must describe the tested build and its limits; untested platforms remain open.

## Required Workflows

- [ ] Create, import, edit, save, reopen, recover, and export a mesh without
  silently discarding frames, normals, UV seams, materials, skins, or tags.
- [ ] Select vertices, edges, faces, and surfaces; transform with numeric and
  viewport controls; extrude, split, merge, duplicate, delete, and repair topology.
- [ ] Inspect and edit UVs against project textures, with seam control,
  projection, island transforms, and predictable material assignment.
- [ ] Author frame animation, attachment tags, and collision geometry, with
  clear target format limits and working idTech1/2/3 export workflows.
- [ ] Use orthographic and perspective views with correct occlusion, accurate
  picking, framing, snapping, and measurable interaction performance.
- [ ] Recover from failed edits, cancelled imports/exports, external source
  changes, and application interruption. Undo/redo includes the relevant
  selection and has a documented memory limit.
- [ ] Share project paths, material navigation, package staging, dependency
  validation, level placement, and compiler inputs with the other studio tools.
- [ ] Expose the same authoring and validation services through a documented CLI,
  with deterministic outputs and actionable text/JSON diagnostics.
- [ ] Verify keyboard operation, screen reader metadata, text scaling through
  200%, both high contrast themes, reduced motion, RTL, and translation expansion.
- [ ] Pass appropriate unit, malformed-input, integration, GUI, performance,
  package, and clean-install tests on Windows, macOS, and Linux.

## Architecture

The editable mesh document builds on `ModelMesh`, the shared native decoder and
viewport representation. Every surface retains a parallel position/normal array
for each frame and a common triangle/UV layout. UV or hard-normal seams remain
separate vertex indices; import must not weld them implicitly. Topology changes
must update every frame. A frame-local position edit must leave other frames
intact. Mesh operations prepare and validate a candidate before committing it.

Primitive `.model.json` designs remain editable parametric sources. Baking a
design into the mesh document is explicit; editing the baked mesh cannot rewrite
or pretend to retain its original procedural parameters. Package native models
also become separate editable sources. Game exports are derivatives, and source
files must be protected from accidental overwrite.

No new renderer, importer library, or build system is assumed by this plan. Any
dependency or rendering change requires the stack/dependency/license records
and cross-platform build integration in the same change.

## Evidence And Remaining Gaps

**Placed MD3 appearances** share compiler skin/default-skin/remap resolution
between Levels, its background camera worker, dependency subset export and CLI
review, including immutable staged package inputs. Instance cache keys prevent
copies of one model from sharing the wrong appearance. Receipts retain exact
skin inputs/hashes, map ownership, effective materials and omitted surfaces.
Existing entity inspector and `map edit` authoring retain undo/redo and save-as.
Independent NRC q3map2 BSP checks cover ten generated scenarios, including a
verified assembly bake for static poses. They exposed the importer's nonzero
MD3 frame bug: the studio now refuses those requests with a concrete remedy.

The optimized Windows C++20/Qt 6.10.1 build passed 21 suite executions: seventeen
core, CLI and integration suites at 1x, followed by four GUI suites at actual 2x
device pixels. The new suites supply 65 core/CLI checks and 67 GUI checks per
scale. All twelve owned camera/details renders were reviewed across dark and
both high-contrast themes, up to 200% text, RTL and expanded translations.
Scoped evidence is under
`.agents/tmp/modeller-rc/evidence/level-model-appearance/`; the final milestone
record binds accepted tests to source and executable hashes. See
[Placed Model Appearances](LEVEL_MODEL_APPEARANCE.md) for the precise contract,
strict validation differences and remaining compiler/format/platform gaps.
This is progress toward the gate, not completion of the modeller goal.

**Model browser appearances** add per-surface material slots, exact package
Quake III `.skin` selection and embedded MDL skin/member inspection to the
ordinary Models browser. The shared service also drives `model materials`,
including staged portable drafts and project palettes. Selected appearance
receipts preserve skin input identity or MDL indexed-pixel/palette hashes.
Preview changes keep the camera and frame, retire stale worker/picker results
and preserve original model bindings for the Mesh Editor and frame export.
See [appearance contracts](MODEL_MATERIAL_SLOTS.md#model-browser-appearances).

The optimized Windows C++20/Qt 6.10.1 build passed twenty suite executions:
sixteen core, CLI, browser, material, native-skin and player-package suites at
1x, then four affected GUI suites at actual 2x device pixels. The new suite
supplies 142 core/CLI checks and 113 GUI checks at each scale. These cover exact
indexed pixels, package palettes and drafts, source preservation, cancellation,
stale worker/picker rejection and original-binding handoff to the Mesh Editor.
All 24 owned appearance and package-picker renders were reviewed across dark,
both high-contrast themes, 200% text, expansion and RTL. Review found and fixed
horizontal overflow, reversed technical labels and clipped wrapped labels.

The accepted executable and compiled modeller/shared inputs are recorded in
`.agents/tmp/modeller-rc/evidence/model-appearance/milestone.json`, together with
test results, raw render dimensions, documentation/catalog checks and cleanup.
One RTL skin render has a taller logical panel after editor handoff; device
scale and dimensions are recorded separately. Tests use owned widget methods
and render targets without OS input or screen capture. An earlier shared-header
change produced inconsistent level-profile startup objects; rebuilding resolved
that failure. Concurrent audio, level, package and documentation changes remain
outside this scoped evidence.

This addresses the ordinary browser selection gap recorded below. The placed
MD3 milestone above adds per-instance level skins and compiler remapping through
their separate compiler contract. Timed MDL skin groups remain available in the
Mesh Editor; the browser
holds the chosen member. Physical accessibility, maximum-size workloads,
original-engine gameplay and clean cross-platform packages remain release gates.

**Native player packages** now connect assembly authoring to deterministic Quake
III PK3 publication. Exactly three native lower/upper/head roles retain their
separate poses, tags and skin assignments; local transforms and inherited scale
are baked into each MD3. The shared service adds the native configuration,
converted icon, material dependency closure and a path-free content receipt.
GUI review and CLI operations use cancellable workers, input guards, immutable
reviewed bytes and the existing atomic package writer. Portable package drafts
carry staged material edits into the same workflow. See
[native player contracts](MODEL_ASSEMBLY.md#native-player-packages).

Compatibility review follows the original game's shader/image filename lookup
and TGA orientation behavior. Unsupported substitutions and image layouts block
publication. A second dependency inspection over captured bytes rejects shaders
that introduce new references during review. The generated receipt reports native
position quantization and normal encoding, without presenting unmeasured errors
as zero.

The optimized Windows C++20/Qt 6.10.1 build passed nineteen suite executions:
fifteen core, CLI, GUI and package/level integration suites at 1x, followed by
four GUI suites at actual 2x device pixels. The new player suite supplies 72
core/CLI checks and 137 GUI checks at each scale. All eighteen player options,
package image picker and reviewed-file renders were inspected across dark,
both high-contrast themes, 200% text, expanded strings and RTL. Selected files
expose their full path and SHA-256 in the scrollable details area.

An independent harness extracts the original Quake III player lookup, skin,
animation and TGA loader functions into a standalone executable. Ten positive
and deliberate missing-input cases passed 24,984 comparisons against synthetic
packages exported by the tested CLI. Model registration and shader resolution
remain explicit shims; this does not establish renderer or gameplay acceptance.
Eight documentation, CLI-help, credit, translation and whitespace checks passed.
Source and executable hashes, native-loader inputs, reviewed renders and cleanup
records are retained under
`.agents/tmp/modeller-rc/evidence/player-bundle/milestone.json`. The evidence
covers those executables and compiled modeller/shared sources; later unrelated
checkout changes and shared Meson test registrations are recorded separately.

Team variants, custom sounds, generated LODs, shader/video runtime behavior,
original-engine gameplay, physical accessibility, maximum-size workloads and
clean cross-platform packages remain open. All ten release gates above remain
unchecked; this is an implementation milestone toward the broader goal.

**Linked assembly skins** now let each part use a loose or exact package-entry
Quake III `.skin` without rewriting its model. Schema 3 stores the reference;
resolution retains separate original model/skin hashes, sizes and mapping
receipts. The worker applies primary material assignments to its snapshot while
preserving alternate materials, geometry, frames, tags and collision data.
Preview, pose baking and animation baking share those assignments. GUI and CLI
edits include validation, cancellation, undo/redo, save/reopen, relative Save As,
recovery and input protection. Missing inputs leave a repairable recipe and clear
the stale preview. See the [linked skin contract](MODEL_ASSEMBLY.md#linked-skins).

This also repairs two existing assembly defects. Material-worker results now
reach the preview through the correct surface keys. Composite names use
`part_s0`, `part_s1`, and so on, avoiding the collision produced when Quake III
strips a final underscore and single character from `part_0` and `part_1`.

The optimized Windows C++20/Qt 6.10.1 build passed sixteen executions: thirteen
skin, assembly, animation, recovery, CLI and package-staging suites at 1x, then
three assembly/native-animation GUI suites at actual 2x device pixels. The new
core/CLI suite supplies 61 checks; the new GUI suite supplies 94 at each scale.
All 18 new skin-control, details and package-picker renders were reviewed across
dark, both high-contrast themes, 200% text, expanded strings and RTL. These use
owned offscreen widgets and render targets; physical accessibility remains open.

The independent FTE harness now starts with unusable source materials, links
two loose skins and one exact package occurrence through the CLI, and verifies
unchanged inputs plus the resulting native MD2 skin and MD3 shader names.
All fifteen native geometry/normal checks passed. Five poses and two blends
produced 28 engine screenshots across a positive case and three deliberate
failure cases. All 168 panel and 24 identity checks matched their expected
outcomes, including detection of stale MD2 poses, a displaced MD3 grandchild
and incorrect child UVs. All seven positive captures and one representative
capture per failure case were visually reviewed. FTE ran through headless
EGL/llvmpipe with engine screenshots and input devices disabled. This proves
baked material handoff in that source port, not live attachments, full shader
evaluation or original Quake III gameplay.

Evidence and exact source/binary identities are retained in
`.agents/tmp/modeller-rc/evidence/assembly-skins/`. The accepted build captured
unchanged sources; both test runs used unchanged binaries and matching compiled
modeller/shared inputs. Later level-editor, audio-test and shared Meson edits
are outside this evidence. The Python engine harness records its own final hashes.
Documentation, CLI help, credits, translation extraction, catalog XML and
whitespace checks passed. During integration, concurrent work repaired malformed
catalog tails and supplied a temporarily missing audio test source; this change
does not certify audio behavior. Disposable failed-run captures and catalog
staging copies were removed after verifying their paths and absence of links.

The later browser and placed MD3 milestones above address skin selection.
Team player variants, package-entry animation-config import, source-port dialects,
maximum-size
assembly performance, original-engine gameplay and clean cross-platform packages
remain open. All ten release workflow gates above remain unchecked.

**Quake III native animation** now connects all 31 authored `animation.cfg`
slots to model assemblies. The editor imports and validates native frame ranges,
lower-body offsets, loop tails, reverse playback and integer-millisecond timing;
the same sampler drives preview and pose/animation baking. Native lower/upper
bindings use assembly schema 2 while ordinary recipes retain schema 1. Changes
share undo/redo, save/reopen, recovery, source protection and documented CLI
operations. Export retains the native header metadata and original model frame
mapping. Composed bakes require their own configuration; see
[native animation contracts](MODEL_ASSEMBLY.md#quake-iii-native-animation).

The optimized Windows C++20/Qt 6.10.1 build passed fourteen executions: eleven
native-animation, assembly, viewport, recovery, CLI and package-staging suites
at 1x, then three assembly/native-animation GUI suites at actual 2x device
pixels. The new core/CLI suite supplies 1,136 checks, including parser limits,
legacy configuration, inactive-slot validation, atomic failures, history,
protected export and sampled composition. Its GUI suite supplies 101 checks
at each scale. All 24 new owned renders were reviewed across dark, both
high-contrast themes, 200% text, expanded strings and RTL, including scrolled
metadata and effective model ranges. Tests use owned widget methods, signals
and render targets without input injection, OS capture or a game launch.

An optional harness compiled the original Quake III parser and playback-step
functions with small file/client-state shims. All 1,185 native metadata,
31-slot parsing and frame-selection comparisons passed. The first run exposed
a double-roundoff error at an exact 66 ms frame boundary; a bounded correction
and direct just-before-boundary regression now preserve both native steps and
interpolation. This compares steady playback selection, not gameplay timing,
transitions, haste or renderer/tag interpolation. Source fragments retain
their upstream GPL notices and recorded hashes. The sampler processed 100,000
updates in 91 ms in the accepted run, a local observation for this small fixture.

Evidence, source/binary identities, diagnostics, reviewed captures and cleanup
records are retained in `.agents/tmp/modeller-rc/evidence/q3-animation/`.
The accepted build captured unchanged sources; both test runs retained matching
modeller/shared C++ inputs and unchanged binaries. The shared Meson file changed
before the 2x run, which used the same built executables. Later package archive,
package staging and Meson sources differ from the accepted binaries and require
another integration build; this evidence does not certify that later checkout.
Documentation, CLI help, credits,
translation extraction, catalog XML and whitespace checks passed. Concurrent
integration exposed a Qt `slots` macro collision and unused sorting wrapper in
package staging, plus an audio button shadowing its dialog's `refresh()` method;
narrow compile fixes were made and the package-staging regression passed.
Other concurrent work resolved missing audio/viewport registration and a Qt
`QSet` overload error. Audio device behavior is outside this evidence.

This remains a scoped milestone in a shared checkout. Package-entry config
import, team player variants, derived runtime slots, source-port
dialects, Quake/Quake II game-code configuration, physical accessibility,
original-engine gameplay and clean cross-platform packages remain open.
All ten release workflow gates above remain unchecked.

**Free trackball rotation** is available inside the Rotate gizmo through its
labelled centre and unoccupied dashed-circle interior. The three constrained
rings retain priority outside the centre. A pure C++ solver lifts normalized
screen positions onto a visible hemisphere/rim, measures from a fixed starting
view and resolves the result into the existing transform basis. Snapping rounds
the total shortest-arc angle while preserving its axis, before XYZ decomposition.
Preview and commit use the same pivot and rotation without a second Euler snap.
Meshes, whole surfaces, tags and static/animated collision share current/all-frame
scope, validation, cancellation, undo and recovery. Existing numeric CLI commands
reproduce the generated mesh transform byte for byte. No schema or dependency
changes were needed; persistent object transforms remain open.

The optimized Windows C++20/Qt 6.10.1 build passed thirteen executions: ten
core/CLI, transform-axis, manipulation, tag, whole-surface and collision suites
at 1x, then trackball, axis and manipulation GUI suites at actual 2x device pixels.
The new core/CLI suite supplies 2,413 checks, including 1,080 independent Qt
quaternion comparisons, both Euler singularities, reflected model bases,
antipodal/rim behavior, snapping, invalid inputs and history/recovery. Its GUI
suite supplies 656/659 checks at 1x/2x across both cameras, all transform bases,
mesh/tag/collision previews, exact commits, cancellation, one-step history and
dark/high-contrast/200%-text/expanded-RTL layouts. Tests operate owned widgets and
render targets without input injection or OS capture. Physical keyboard and
screen-reader acceptance remains open.

A 65,536-vertex, 130,050-face fixture across sixteen poses (1,048,576 pose vertices)
completed load, 32 coalesced free-drag updates, preview, all-frame commit and undo
in 2,355 ms at 1x and 2,883 ms at 2x. Maximum event-loop gaps were 64.6 and 50.7 ms,
under the unchanged 200/300 ms budgets. The pure solver processed 100,000 updates
in 22 ms. These are local observations for this fixture, not bounds for every
topology or combined asset workload. Source/binary identities, test diagnostics,
visual review and cleanup are retained in
`.agents/tmp/modeller-rc/evidence/trackball/`. This scoped milestone leaves all
ten workflow gates above open, including clean cross-platform packages,
original-engine gameplay, remaining geometric/UV workflows and runtime collision
animation export.

Fifty-one owned renders were reviewed, including all thirteen new capture pairs
at 1x/2x and existing transform/tag/collision layouts. Documentation, CLI help,
credits, translation extraction, catalog XML and whitespace checks passed.
The accepted build and both runs retained identical modeller/shared inputs.
After testing, concurrent application-shell and Meson edits changed those
integration inputs. A new configuration attempt stopped because Meson referenced
the then-absent `src/tests/audio_recording_smoke_test.cpp`. The modeller sources
still match the recorded binaries; the later shared checkout is not certified.
The unrelated audio work was preserved for its owner to complete.

**Animated collision authoring** now retains one oriented-box pose per stored
mesh frame, alongside unchanged static boxes. Animate, freeze, per-frame fitting,
current/all-frame updates and transforms share document validation, undo,
fingerprints, recovery and CLI services. Frame copy, duplication, deletion and
in-between insertion carry tracks with them. Preview and generated poses share
linear centre/size interpolation and shortest-arc quaternion orientation.
Inspector values, selection axes, local scale handles and visible-box framing
follow the displayed pose. Version-7 sources preserve mixed static/animated
collision without dropping native MDL data or earlier source compatibility.

Map export and placement require an explicit stored frame for animated sources.
GUI handoff pauses playback and supplies the current frame. Results are static
brushes with a pose-specific note and one level undo step. Native model formats
and assembly baking continue to disclose omitted collision. This adds no physics
library or runtime dynamic-collision export.

The optimized Windows C++20/Qt 6.10.1 build passed sixteen test executions: fourteen
core, CLI, collision GUI, animation, transform-axis, fingerprint, recovery,
native-export and assembly suites at 1x, then both collision GUI suites at actual
2x device pixels. The new core/CLI suite supplies 84 checks, including strict
schema parsing, track copy isolation, pose-zero deletion, native MDL coexistence,
maximum-track history accounting, cancellation and sampled-map equivalence for
Quake, Quake II and Quake III. Sixty owned Qt renders were reviewed across dark,
both high-contrast themes, 200% text, expanded RTL labels and transform modes.
The tests use semantic widget calls and render targets, without input injection
or OS capture. Physical keyboard and screen-reader acceptance remains open.

The 64-box by 1,024-pose fixture produced a 4,015,556-byte source; serialize,
validate, parse and reserialize took 624 ms in the accepted run. Its GUI load,
all-pose edit, undo and preview took 593 ms at 1x and 1,581 ms at 2x. Maximum
event-loop gaps were 13.4 and 46.0 ms, under the 200/300 ms audit budgets.
These are local observations for maximum collision tracks with small mesh
geometry, not bounds for every combined maximum-geometry workload.

Evidence, binary/source identities, diagnostics, reviewed captures and cleanup
records are retained in `.agents/tmp/modeller-rc/evidence/collision-animation/`.
The accepted build and both runs retained stable modeller/shared source inputs.
Documentation, CLI help, credits, catalog XML, extraction and whitespace checks
passed. This is a scoped milestone in a shared checkout; clean packages,
cross-platform releases, original-engine gameplay, arbitrary convex collision
and runtime collision animation export remain open. All ten workflow gates
above remain unchecked.

**Dense UV previews** now prepare selected-face winding contours and share the
studio's antialiased CPU wire renderer. Selection fills cancel opposite indexed
borders and simplify exactly collinear contour segments, retaining holes,
overlaps and reversed UV faces. Hatching paints the selected union once. Every
indexed wire and pick identity remains present. Ordinary wires, seams and
selected edges draw in that order; selected markers remain visible at coincident
corners. Opaque tile reuse bypasses only pixels proven unchanged by a stroke of
the same color. Cancellation polls inside long lines, and the 4,194,304-pixel
image ceiling now also holds for extreme aspect ratios. Move fills use the same
float coordinate addition as wires and document edits, including large UVs.

The optimized Windows C++20/Qt 6.10.1 application passed twelve test executions:
ten focused renderer, UV, packing, core/CLI, 3D viewport and level-plan wire
suites at 1x, then both UV editor suites at actual 2x device pixels. The new
renderer suite supplies 110 checks. An independent triangle-coverage oracle
checks holes, clipping, disconnected regions, folds, opposite winding,
duplicates, overlaps and nonmanifold UV edges. Other checks cover overlay
priority, fractional scaling, exact tiled/scalar wire agreement, move isolation,
large-coordinate rounding, extreme image dimensions and atomic cancellation
through final publication. The existing 240 independent wire-area comparisons
and level-plan navigation/theme/scale regressions also pass.

At 1,024×768 logical pixels, with topology prepared separately, the fully
selected 65,536-vertex fixtures measured:

| UV fixture | Device scale | Original render | Updated render |
| --- | --- | --- | --- |
| 130,050-face grid | 1x | 898 ms | 136 ms |
| 130,050-face grid | 2x | 1,333 ms | 525 ms |
| 65,534-face strip | 1x | 29,642 ms | 273 ms |
| 65,534-face strip | 2x | 74,698 ms | 1,407 ms |

All eight selected/unselected benchmark cases passed the unchanged 3,000 ms
render budget. These are local observations under variable concurrent load,
not a universal speedup or a bound on every contour/overlap arrangement. The
production UV worker additionally completed the fully selected grid/strip in
232/273 ms at 1x and 282/411 ms at 2x, against a 5,000 ms completion budget.
Maximum event gaps stayed below 8 ms against 200/300 ms budgets. The sixteen-pose
packing/preview/undo workflow completed in 0.95/2.06 seconds with maximum event
gaps of 14.6/52.4 ms. Exact indexed-face picks remain correct in both dense layouts.

All 39 accepted renders were inspected, including fractional scales, dark,
both high-contrast themes, 200% text, expanded strings and RTL. Tests use owned
semantic calls and widget render targets, without input injection, OS capture
or game launches. Evidence, source/binary identities, timings and retained
renders are in `.agents/tmp/modeller-rc/evidence/uv-render/milestone.json`.
The application SHA-256 is
`f7706b9a0caf7c902dc22f618ee9ba08d39c7dc81fa3b852445f3c82179c3965`.
Captured modeller/CLI/build and shared wire inputs stayed unchanged through
the accepted build and tests. Concurrent audio and other studio edits are
recorded separately; this does not certify the shared checkout or a package.
After accepted testing, the shared application shell and Meson file changed
again. Their later hashes are recorded outside the tested binaries; modeller,
CLI, model-test and shared wire sources still match the accepted inputs.
No dependency, document schema, authoring service, CLI option or export format
changed. Physical accessibility, broader interaction throughput, clean
cross-platform packages, original-engine gameplay and all ten release workflow
gates remain open.

**Pack Around Unselected** now packs complete UV islands into free space around
fixed unselected faces and other surfaces sharing a material slot. Material
identity follows package path normalization and case folding, including alternate
slots. Fit uniformly finds one common scale; Keep current UV scale preserves
texel density and refuses if the islands do not fit. Both retain orientation,
rectangular pixel proportions, all pose geometry, normals and material metadata.
Shared corners split across every pose and retain their seam marks. The editor
and `model edit --operation uv-pack-around` use normal document history,
save/reopen, recovery and native export. See [UV contracts](MODEL_MESH.md) and
[CLI options](CLI_STRATEGY.md).

The optimized Windows C++20/Qt 6.10.1 build passed ten test executions: nine
focused document/UV/core/CLI/editor suites at 1x and the new editor suite at
actual 2x device pixels. New coverage provides 144 core/CLI checks and 92/95 GUI
checks at 1x/2x. An independent continuous-triangle distance oracle verifies
border and fixed/selected island gaps, including sloped charts, rectangular
atlases, non-word-aligned bitmap widths and zero padding. Tests also cover
determinism, exact fixed attributes, all-pose seam splits, normalized alternate
material paths, full-atlas refusal, partial-island expansion, invalid UVs,
allocation/work limits and cancellation through final publication. CLI dry-run,
overwrite/source protection, inline options and mixed duplicate rejection pass.
Existing island, xatlas, rectangular-atlas and document regressions pass.

A selected strip and fixed painted panel reach 65,536 vertices and 1,048,576
pose vertices across sixteen poses, with 65,532 faces. Opening, packing, UV
preview and undo through production workers took 4.28/13.97 seconds at 1x/2x;
maximum event-loop gaps were 32.4/59.0 ms against 200/300 ms budgets. This is a
bounded local workload, not the maximum-face grid or a throughput guarantee for
every chart topology. Broader UV preview and packing performance remain part of
the release audit. All twelve before/after renders were inspected across dark,
both high-contrast themes, 200% text, expanded strings and RTL. They show all
three project materials resolved, fixed painted regions unchanged, moved islands
in free space and usable packing controls. Tests use owned widget signals and
render targets, with no OS input injection, OS capture or game launch.

Evidence is retained in `.agents/tmp/modeller-rc/evidence/uv-obstacles/milestone.json`,
including 284 captured modeller/CLI/build inputs, binary identities, commands,
diagnostics, timings and reviewed renders. The application SHA-256 is
`23b0ecb01b062f0db1be4986d52eadd3806c60eb4cc90ef89a84569bcd422d46`.
Those inputs remained unchanged through the accepted build and tests. A
concurrent property-inspector extraction temporarily interrupted an earlier
application link; reconfiguration picked up its registered source without
unrelated edits. Other shared studio changes are recorded separately. This is
scoped Windows evidence, not certification of the shared checkout or a package.
Subsequent level-list changes to the shared shell and Meson inputs are recorded
with their new hashes and are outside this milestone's tested binaries; the
modeller, model-test and CLI sources still match the accepted inputs.

Fixed UVs must remain in the 0–1 tile. The bounded conservative search may leave
space unused or refuse a layout that another arrangement could fit. Shader
aliases, persistent per-corner pins, multi-tile packing and texture rebaking
remain open. No dependency or source schema changed. All ten release workflow
gates remain open, including physical accessibility, clean cross-platform
packages and original-engine gameplay acceptance.

**Repair Import…** now prepares damaged mesh JSON and complete MDL/MD2/MD3
sources before normal editable-model admission. It proposes all-pose removal
of invalid, repeated-index or collapsed faces, rebuilds only unusable normals
and removes orphaned seam marks. The review includes exact original indices,
first collapsing poses, explicit +Z normal fallbacks and a prepared-copy
viewport. Selecting a pose-specific change updates that preview. Save and Open
Copy requires a new source and verifies the reviewed input's contents and
resolved location before saving and immediately before publication. The saved
copy then uses normal editing, undo, recovery, export and studio handoff.
The shared `model repair-import` CLI provides text/JSON review, dry-run and
new-copy save. See [repair contracts](MODEL_MESH.md) and [CLI](CLI_STRATEGY.md).

The optimized Windows C++20/Qt 6.10.1 build passed ten test executions: nine
focused core/CLI/editor suites at 1x and the repair editor suite at actual 2x
device pixels. New coverage supplies 70 core/CLI checks and 101 GUI checks at
each scale. It verifies exact surviving attributes, native MDL groups/skins and
MD2/MD3 poses/tags, original face identities, selective normal rebuilding,
all-pose removal, CLI parity, atomic cancellation and refusal of incomplete
decodes. Source changes during review, temporary-file writing and the final
commit checkpoint block publication and adoption. Late cancellation after a
successful commit still adopts the saved copy. Ordinary edit/undo/reopen,
document, native format, OBJ, health, intersection and recovery regressions pass.

The full 65,536-vertex, 130,050-face grid across sixteen poses repairs 1,048,576
normals without removing faces or using fallbacks. Preparation with exact result
fingerprint verification took 4,296 ms locally. The production GUI prepares,
reviews a million-row report, saves, opens and renders that copy with maximum
event-loop gaps of 66.9/68.9 ms at 1x/2x, below the configured 200/300 ms budgets.
These are bounded local measurements under shared machine load. All twelve
review/adopted-editor renders were inspected across dark, both high-contrast
themes, 200% text, expanded strings and RTL. Exact indices remain visible; long
descriptions use horizontal scrolling and a full selected-change detail line.
Destination, save, cancel, preview and ordinary editor controls remain usable.
No mouse/keyboard injection, OS capture or game launch was used.

Evidence is retained in
`.agents/tmp/modeller-rc/evidence/import-repair/milestone.json`, including
277 captured modeller/CLI/build inputs, exact binaries, commands, diagnostics,
timings and reviewed images. The tested application SHA-256 is
`2a084d0b91079f0aa9f13c7a7db4c75199806979c28867c06d1f54db00533ed8`.
Those modeller inputs remained unchanged through the accepted build and tests.
A transient unrelated level-editor include failure interrupted an earlier
build and was already corrected by concurrent work when inspected. Other
concurrent studio edits are recorded separately; this is scoped Windows
evidence, not certification of the shared checkout. No dependency or source
schema changed.

This advances the source-admission gap without claiming general damaged-file
recovery. Incomplete decodes, decode warnings, missing arrays, invalid metadata,
invalid positions/UVs, all-face loss and excessive workloads are refused. It
does not repair malformed OBJ polygons, truncated native records, intersections,
winding or duplicate faces. Strict normal loading remains unchanged. Every
release workflow gate remains open, including native physical accessibility,
clean cross-platform packages and original-engine gameplay acceptance.

Mesh Health now provides **Inspect Intersections** for geometric face crossings
and coplanar area overlaps, within and between surfaces across all stored poses
or the current pose. The shared `model intersections` CLI emits exact, sorted
pose/surface/face pairs and contact types. Findings navigate to either face and
its pose for normal authoring, including poses outside the current preview clip.
Document edits and current-pose playback invalidate stale navigation. Scanning
runs on the cancellable document worker and changes no geometry, selection,
source, history or recovery state. Results are informational; intentional
overlap remains an authoring choice. See [geometric inspection](MODEL_MESH.md)
and [CLI inspection](CLI_STRATEGY.md).

The optimized Windows C++20/Qt 6.10.1 build passed nine test executions: eight
focused core/CLI/editor suites at 1x, then the intersection editor suite at
actual 2x device pixels. New fixtures provide 102 core/CLI checks and 112 GUI
checks at each scale. Analytic contacts, winding/order variants and 48 seeded
plane crossings exercise scale, translation and axis changes. Coverage includes
self and cross-surface pairs, all/single-pose scope, either side of a surface
filter, source preservation, bounded atomic failures, cancelled scans, exact
navigation, edits/undo, stale playback results and a 65,341-finding report.
The next dense fixture exceeds the real 65,536-finding ceiling and fails
without adopting a partial report. Boundary fill, boundary bridge, mesh health,
document and playback regressions also pass after sharing the contact predicate.

A 65,536-vertex, 130,050-face grid across sixteen poses scans 2,080,800 face
poses with 17,589,008 candidate pairs and 90,465,312 spatial-node visits. The
direct scan took 13,055 ms locally. Editor load, scan and dense-positive-report
work stayed below 59.6/99.1 ms maximum event-loop gaps at 1x/2x, against
configured 200/300 ms budgets. These are bounded local measurements under
shared machine load, not worst-case scene guarantees. All twelve new first/
second-face widget renders were reviewed across dark, both high-contrast
themes, 200% text, expanded strings and RTL. Scope, details, controls, focus and
selected faces remain legible. No input injection or OS capture was used.

Evidence is retained in
`.agents/tmp/modeller-rc/evidence/intersections/milestone.json`, including
266 captured modeller/CLI/build inputs, exact binaries, commands, diagnostics,
timings, catalog/documentation checks and reviewed images. The tested
application SHA-256 is
`1b1190c1624df503c197198248d54111d98f1864eeb37a2172fe9cf84d5d4bfb`.
The accepted build had stable captured inputs; modeller inputs and binaries
remained unchanged through the no-rebuild tests. Concurrent level/studio work
is recorded outside this validation. No new unrelated product defect was
found. This is scoped Windows evidence; it does not certify the shared checkout.
No dependency or source-schema change was introduced.

The scan accepts validated editable meshes and uses scale-relative floating-
point predicates. Shared boundary segments, isolated point contacts, solid
containment without surface crossing and motion between stored poses are
outside its contract. It performs no automatic repair and does not certify
exact geometry or watertightness. General malformed-source admission/repair,
physical accessibility, original-engine gameplay, clean cross-platform
packages and all ten release workflow gates remain open.

Mesh Health now offers **Split Nonmanifold Edges**, also available through
`model edit --operation split-nonmanifold-edges`. For indexed edges shared by
more than two faces, the shared service separates endpoint face fans using
existing two-face connections. It preserves those connections, every triangle
and its winding, UVs, authored normals and all animation poses. The result is
deterministic and independent of pose geometry. Selection and seam marks follow
actual face corners; unrelated surfaces, tags, clip timing and unused vertices
remain intact. Repair is one cancellable document edit with undo, redo and
recovery. Core and CLI reject a single-pose topology repair. See
[mesh health and repair](MODEL_MESH.md) and [CLI authoring](CLI_STRATEGY.md).

The optimized Windows C++20/Qt 6.10.1 build passed nine test executions: eight
focused core/CLI/editor suites at 1x and the Health editor suite again at actual
2x device pixels. The new fixture provides 73 core/CLI checks; the expanded
Health fixture passes 148/151 GUI checks at 1x/2x. Coverage includes a closed
tetrahedron with an attached fin, duplicate faces, unrelated disconnected fans,
24 seeded indexed complexes, all-pose corner identity, existing two-face
adjacency, source/recovery and MD2/MD3 round trips, exact CLI parity and output
protection. Cancellation during planning, copying and final validation is
atomic. Vertex, frame-storage and whole-document budgets reject excess work
without changing the document or result maps.

A 21,845-face book fixture produces 65,535 vertices across sixteen poses
(1,048,560 frame vertices). The direct repair took 211 ms locally. Editor load,
inspection, repair with its follow-up scan, and undo stayed below 10.8/55.6 ms
maximum event-loop gaps at 1x/2x against configured 200/300 ms budgets. These
are bounded local measurements, not general production-scene guarantees.
All twelve new before/after widget-owned images were reviewed across dark,
both high-contrast themes, 200% text, expanded strings and RTL. Labels, focus,
scope and findings remain legible; the three new boundary edges stay selected.
No input injection or OS capture was used. Generated help, documentation,
CLI help, credits, source-string extraction, whitespace and all 21 catalog XML
checks pass.

Evidence is retained in
`.agents/tmp/modeller-rc/evidence/nonmanifold/milestone.json`, including commands,
254 captured modeller/CLI/build inputs, diagnostics, timings and reviewed images.
The tested application SHA-256 is
`e96161b1c4d88e0dca46a2b7a8a906e30f3fbb62554907947b0032c1ca5fe560`.
The captured modeller implementation and tested binaries remained unchanged
through the accepted build and no-rebuild runs. Concurrent documentation and
audio edits are outside this validation. Disposable probe and older regression
captures were removed; their test diagnostics remain. No new unrelated product
defect was found. This is scoped Windows evidence, not release certification
for the shared checkout. No dependency or source-schema change was introduced.

Splitting preserves geometry and may create open boundaries. It does not delete
duplicate faces, fill holes, determine outward winding, repair geometric
intersections or cut a nonorientable two-face surface. Those operations require
separate inspection and authoring decisions. Broader repair/import admission,
physical accessibility, original-engine gameplay, clean cross-platform packages
and all ten release workflow gates remain open.

Sampled assembly animation now has an independent native-engine acceptance
workflow. The CLI authors a three-part chain with animated attachment origins
and orientations, nested local transforms/scales, distinct playback rates,
fractional phase, looping, clamping and held poses. Five samples from a nonzero
start time bake to schema-6 mesh and multi-surface MD3; compatible surfaces are
explicitly joined for MD2 export. Closed-form geometry checks every native pose
and MD3 normal within encoding precision, without the studio decoder. FTE then
renders five poses and two native vertex blends, checking each part's projected
silhouette and texture quadrants. Three deliberate stale-pose, nested-position
and UV controls fail exactly their predicted panels while preserving the others.
See [baked assembly engine acceptance](MODEL_ENGINE_ACCEPTANCE.md#baked-assembly-animation).

The optimized Windows C++20/Qt 6.10.1 build passed six focused core/CLI suites,
including 59 assembly-animation assertions. Both optional FTE workflows passed
all eight engine cases: 48 registered render-target screenshots, 228 panel
checks with expected positive/negative outcomes, and 68 engine identity checks.
All seven new positive captures and one affected capture from each new control
were visually reviewed. The shared offscreen runner preserves the original
MDL/MD2/MD3 render regression, including all three older controls. No input
injection, desktop capture, fullscreen session or commercial game data was used.

Evidence is retained in
`.agents/tmp/modeller-rc/evidence/assembly-engine/milestone.json`, including build
inputs, commands, independent byte errors, exact tools and image hashes. The
tested application SHA-256 is
`0bf30c39fa0eee023b92a9101a265d030555e464f51e2ab42ba6c098b05987c9`.
The final incremental build had stable captured inputs. A subsequent unrelated
Meson registration edit occurred during no-rebuild core tests; the tested
binaries and modeller implementation stayed unchanged. This records the tested
build, not the entire later shared checkout. The new acceptance code and
documentation add no production dependency or model-schema change.

This closes the small generated baked-animation FTE fixture gap. Native game
timing configuration, live engine attachments, full lighting/shader behavior,
runtime collision animation export, maximum-size assemblies, original-engine gameplay and
clean cross-platform release packages remain open. All ten release workflow
gates above remain unchecked.

World, Selection and Custom transform axes now share one service across Geometry,
viewport move/rotate/scale gestures and CLI. Selection derives a deterministic
basis from a usable face, the active whole surface, a tag or a collision box.
One displayed/reference pose supplies fixed axes across affected frames. Custom
XYZ orientation also applies to face extrusion and duplication. Normals use the
corresponding inverse transpose; collision sizing retains intrinsic box axes to
avoid shear. Axis controls are session/operation values, while the resulting
geometry uses existing source, recovery, native-export and studio handoff paths.
See [transform axes](MODEL_MESH.md) and [CLI options](CLI_STRATEGY.md).

The optimized Windows C++20/Qt 6.10.1 build passed 27 test executions: eighteen
core, CLI and GUI suites at 1x, followed by nine GUI regressions at actual 2x
device pixels. New fixtures provide 84 core/CLI checks and 256/259 GUI checks at
1x/2x. Independent cyclic and oblique transform arithmetic, normal handling,
all-frame/current-frame selection, reflected tags, collision sizing, degenerate
selection refusal, cancellation, history, source/recovery round trips and MD3
export pass. Every oriented axis is checked in orthographic and perspective
previews against committed geometry. Discrete playback refreshes visible axes
before pausing. CLI spaced/inline forms, mixed duplicates, usage errors, dry-run
and source protection pass. A test extrusion initially used a direction tangent
to the second pose: the existing atomic geometry refusal was retained and tested
alongside a valid oblique direction. Inline CLI duplicate counting was corrected.

The 65,536-vertex, 130,050-triangle, sixteen-pose editor fixture includes axes
changes and a last-face scan. Those four axes stages stayed below 7.1 ms maximum
event-loop gaps at both display scales; the full editor regression stayed below
22 ms against its configured 200/300 ms budgets. These are measured local fixture
results, not universal production-scene guarantees. All twelve widget-owned
Selection/Custom images were reviewed across dark, both high-contrast themes,
200% text expansion and RTL. Numeric focus, progressive controls and labelled
handles remain legible. No input injection or OS capture was used.

Evidence is retained in
`.agents/tmp/modeller-rc/evidence/transform-axes/milestone.json`, including commands,
diagnostics, 248 captured modeller/CLI/build inputs and reviewed images. The tested
application SHA-256 is
`6dd431a7ca7705129bda434a5ca37fa7c1dd9fcd640612996e50453aaf88dffb`.
The modeller implementation and model-test inputs remained unchanged through the
accepted build and tests. Shared `src/meson.build` changed during the 2x run;
that edit is recorded outside validation, and all tested binaries stayed unchanged
through both no-rebuild runs. Generated help, extraction, credits, whitespace and
all 21 catalog XML checks pass. Disposable captures and catalog copies were removed.
No new unrelated product defect was found. This is scoped Windows evidence, not
a release-certified checkout. Selection axes are recalculated for each operation;
persistent object transforms remain separate work. Free trackball rotation is
covered by the later milestone above.
Physical accessibility, native game acceptance, clean cross-platform packages
and all ten release gates remain open.

UV atlases now support independent width and height through the Surface
inspector and `model edit --uv-atlas-size WIDTHxHEIGHT`. The square default,
integer CLI form and existing C++ option ordering remain compatible. Same as
width links the GUI dimensions; padding follows the shorter axis. The existing
pinned xatlas build adaptation carries separate axis limits through packing,
with uniform chart scaling and a bounded density search. Repacking preserves
existing chart shape, orientation and relative scale in texture pixels.
Animation poses, authored normals, materials, tags and clip timing remain intact.
See [UV atlas authoring](MODEL_MESH.md).

The optimized Windows C++20/Qt 6.10.1 build passed 21 test executions: fifteen
core, CLI and GUI suites at 1x, followed by six GUI regressions at actual 2x
device pixels. New fixtures provide 78 core/CLI checks and 93/96 GUI checks at
1x/2x. Independent pixel-distance checks cover wide, tall, non-power-of-two and
extreme-aspect atlases; chart coverage verifies useful long-axis placement.
Determinism, square compatibility, invalid dimensions, short-axis padding,
allocation failure and cancellation pass. Document undo/redo, save/reopen,
recovery, MD3 export, CLI parity, dry-run and overwrite protection also pass.
Existing UV, island, atlas, material, document and boundary-bridge suites remain
green. An initial GUI fixture stayed in Solid mode after loading a material;
selecting Material mode corrected the fixture without changing preview behavior.

All twelve before/after widget-owned images were reviewed across dark at 100%
text, both high-contrast themes with expanded labels at 200% text, RTL and
1x/2x scaling. Width, height, square link, padding and both atlas actions fit
together in the scrolled inspector. Numeric entry and focus remain legible;
the resolved 512x128 package texture shows the expected pixel proportions.
Generated help, credits, whitespace, source-string extraction and all 21
catalog XML checks pass. Eleven disposable scratch files were removed.

Evidence is retained in
`.agents/tmp/modeller-rc/evidence/rectangular-atlas/milestone.json`, including
commands, source/binary identities, diagnostics and reviewed images. The tested
application SHA-256 is
`b0725205726c329efdcb7ca8cc48faf743222137bd30d780041a004351eba80c`.
All 242 captured modeller, CLI, shell, model-test, xatlas and build inputs stayed
unchanged through the accepted build and test matrix. Shared `src/meson.build`
and application-shell files changed after the initial successful evidence audit;
these later edits are recorded separately and are outside this validation.
The original xatlas source/header
hashes and MIT/BSD notices remain intact. No new unrelated product defect was found.
This is scoped Windows implementation evidence; the shared checkout and other
platforms are not release-certified.

Both dimensions remain bounded to 32–4096 pixels, with the existing memory,
vertex, animation-storage and overlap-validation limits. Packing is heuristic;
optimal occupancy is not guaranteed. In Unwrap and Pack Selected UVs, unselected
UVs are not obstacles; the separate Pack Around Unselected operation now handles
fixed regions. Persistent per-corner pins and texture rebaking remain separate work. Atlas dimensions are
session controls; resulting UVs persist, while MD2 skin dimensions remain an
explicit export-related edit. Physical accessibility, native game acceptance,
maximum-production performance and clean cross-platform packages remain open.
All ten release gates are still unchecked.

Boundary bridging now joins exactly two selected closed loops through Geometry
and `model edit --operation bridge-boundary-loops`. Equal and unequal vertex
counts are supported, with closest-pair reference-pose alignment and an explicit
wrapping twist offset. Two bounded strip candidates share filling's boundary
discovery and proposed-face intersection checks. The accepted strip must remain
valid in every stored pose; cancellation or failure leaves the whole document
unchanged. Existing positions, normals, UVs, seams, materials, tags, collision,
clip timing and other surfaces remain exact. New faces stay selected for
Detach/Unwrap, UV projection and normal finishing. No new dependency or source
schema is introduced. See [boundary authoring](MODEL_MESH.md).

The optimized Windows C++20/Qt 6.10.1 build passed 19 test executions: 14 core,
CLI and GUI regressions at 1x, then five GUI regressions at actual 2x device
pixels. Bridge fixtures contain 60 core/CLI checks and 66/69 GUI checks at
1x/2x. Independent topology and volume checks cover equal/unequal loops,
deterministic seed choices, positive/negative twist wrapping, small and large
coordinates, later-pose collapse, obstruction, malformed inputs, workload limits
and cancellation. Save/reopen, undo/redo, recovery, MD3 export and atlas finishing
pass. Existing boundary filling, health, topology, document workers, recovery,
UV atlas and surface-selection regressions also pass.

All twelve bridge before/after widget-owned images were reviewed: dark at 100%
text, both high-contrast themes with expanded labels at 200% text, RTL and
1x/2x display scaling. Bridge/twist controls fit together in the scrolled
inspector, with visible focus and stable numeric direction. The adjacent Fill
workflow also retains its expanded RTL layout. These are offscreen semantic and
render checks; physical keyboard/screen-reader and small-monitor acceptance
remain open. Generated help, credits, whitespace, source-string extraction and
all 21 catalog XML checks pass. Disposable task copies were removed.

Evidence is retained in
`.agents/tmp/modeller-rc/evidence/boundary-bridge/milestone.json`, including exact
commands, source/binary hashes, diagnostics and renders. The tested application
SHA-256 is `ba9642849b793546cdf15fc0c718c98ef903f2a5f616aadfe97b2955840b03e1`.
All 228 captured model, CLI, shell and model-test sources remained unchanged
through final validation. Other studio work changed unrelated shared files, so
this is scoped implementation evidence rather than whole-checkout release
certification. No new unrelated product defect was found. The first probe's
overwrite expectation was corrected to retain the existing explicit,
fingerprint-checked in-place editable-source save behavior.

Candidate search is not exhaustive. Each loop is limited to 1,024 vertices and
an operation to 16,777,216 work checks. Only proposed faces on the active surface
and stored poses are checked; existing/cross-surface intersections and motion
between poses remain outside this tool. Intermediate rings and automatic
UV/normal finishing are separate edits. All ten release gates remain open.

Assemblies can now bake a uniformly sampled animation into the Mesh Editor or
export it through the shared GUI/CLI service. `model assembly bake-animation`
accepts a start time, frame count and fractional sampling FPS. Each pose follows
the original independent part rates, phases, loops, interpolation and attachment
transforms. The result retains composed geometry, authored normals, topology,
UV seams and external material paths across every sample. Frame storage limits,
incompatible reflected attachment changes and cancellation reject the complete
candidate. Editable mesh, MD2 and MD3 output uses the existing guarded writer;
the recipe, linked models and package inputs remain protected. See
[assembly animation](MODEL_ASSEMBLY.md).

Saved clip FPS is undoable and survives save/reopen and recovery. Optional mesh
schema 6 stores positive fractional rates while existing sources without timing
keep their previous schema. The Mesh Editor and Models browser adopt saved rates;
session preview overrides survive frame refresh without changing source timing.
Native MDL groups keep their independent timing. Game exports still require the
appropriate external animation configuration. Attachment graphs, collision boxes,
embedded skins and native MDL metadata remain in the original assembly inputs;
the bake review explains these limits before publication.

The accepted optimized Windows matrix passes twenty named tests at 1x and five
GUI executions at 2x. New tests contribute 59 core/CLI checks and 57 GUI checks
at each display scale, covering exact sampled geometry, mixed schema metadata,
fractional playback, history/recovery, immutable inputs, guarded exports,
malformed options and cancellation. Existing animation, assembly, document,
recovery, MDL, playback and collision suites pass alongside them. Twelve
widget-owned renders were reviewed across normal dark, both high-contrast themes,
200% text, expanded translations, RTL and actual 1x/2x display pixels. Review
notes now wrap long text and use the dialog's remaining space. The existing
animation layout test now scrolls complete controls into view before checking
their bounds, avoiding Qt's caret-only visibility shortcut for spin boxes.

Exact build, source and binary identities, commands, diagnostics and renders are
retained in `.agents/tmp/modeller-rc/evidence/assembly-animation/milestone.json`.
All modeller production files, Models browser integration, CLI dispatch and tested
binaries remained stable through the accepted matrix. Later shared application
shell edits are recorded separately; this evidence does not certify those edits
or the resulting current checkout. Unrelated concurrent map
and audio edits caused earlier transient build failures. Malformed translation
catalog closing fragments were also found during concurrent catalog writes;
staged extraction, XML validation and byte-guarded atomic publication repaired
them without discarding translated messages. Repair backups remain in the
evidence. This is scoped shared-checkout evidence, not a clean release package.
Maximum-assembly performance, native game acceptance, runtime collision animation export, other
platforms and human accessibility acceptance remain open. All ten release gates
remain unchecked.

Independent UV island transforms now share one service between the Surface
inspector and `model edit --uv-pivot-mode islands`. Complete charts scale and
rotate about their own bounds before adding a shared snapped offset. Projection
uses each chart's bounds in the selected pose. Explicit island expansion converts
component selections; partial charts are otherwise rejected. Shared corners split
deterministically across every pose, preserving exact geometry, authored normals,
unselected UVs, material bindings and other surfaces. Capacity, cancellation,
validation, selection-aware history, recovery and native MD3 export are covered
by the scoped evidence below. See [UV authoring](MODEL_MESH.md).

The accepted optimized Windows matrix passes fourteen named tests at 1x and four
GUI executions at 2x. The new tests contribute 71 core/CLI checks and 57 GUI
checks at 1x (63 including render-settling and pixel-budget assertions at 2x).
They cover per-chart numerical results, seam and point-contact splitting,
nonmanifold boundaries, exact all-pose attributes, projection planes, mirrored
UVs, selection expansion, capacity, cancellation, deterministic ordering, undo,
recovery, source protection and MD3 output. Existing UV/atlas, document, material,
surface-selection, recovery and manipulation tests pass alongside them.

Twelve widget-owned renders were reviewed across dark, both high-contrast themes,
200% text, expanded translations, RTL and actual 1x/2x display pixels. Final 1x
outputs were verified byte-identical to the reviewed copies. The test fixes
retain renderer settling and its existing pixel budget; they do not change the
renderer. Exact commands, hashes, diagnostics and images are retained in
`.agents/tmp/modeller-rc/evidence/uv-islands/milestone.json`. All 127 modeller
production files, CLI dispatch and tested binaries remained stable during the
accepted matrix. An earlier unrelated audio link failure occurred during
concurrent edits and cleared on rebuild; its log remains in the evidence.

This is scoped Windows evidence from a shared checkout. Independent pivots use
complete islands on one active surface; pinned charts, rectangular/free-space
packing and texture rebaking remain open. Original-engine/gameplay, clean
packages on all platforms and human accessibility acceptance remain release
work. All ten release gates remain unchecked.

Whole-surface selection now lives in document state and feeds the extended table,
viewport picks/highlights, numeric transforms, move/rotate/scale previews, history,
recovery and `model edit --surfaces`. One reference-pose bounding box supplies a
common pivot, including unused vertices. Current/all-frame scope and snapping
remain shared; numeric mirroring requires all frames. Mixed component/whole
selections and unsupported operations are rejected before mutation. Join starts
with the persistent set and remaps it to its surviving target. Component-level
selection still belongs to one surface. Tags and collision remain separate and
normal mesh saves/exports omit selection. See
[surface transforms](MODEL_SURFACES.md#select-and-transform-surfaces).

The accepted optimized Windows matrix passes fifteen named tests at 1x and four
GUI executions at 2x. The new suite contributes 112 core/CLI checks and 114 GUI
checks at 1x (117 with the extra DPI checks at 2x). It verifies exact geometry,
common pivots, snapping, mirroring, native MD3 export, invalid/mixed selectors,
source protection, old recovery compatibility, cancellation, history, active
table focus, Join initialization and preview/commit agreement. Existing component,
UV, tag, collision, material and recovery suites also pass. Twelve widget-owned
renders were reviewed across normal dark, both high-contrast themes, expanded
translations, RTL, 200% text and actual 1x/2x pixels. Visual review identified
count columns outside the table viewport at large text sizes; adaptive minimum
width and a visibility assertion now keep names and both counts visible.

Commands, diagnostics, hashes and renders are retained in
`.agents/tmp/modeller-rc/evidence/surface-selection/milestone.json`. All 126
modeller production files, CLI dispatch and tested binaries remained stable
during the accepted matrix. Other studio work changed the shared checkout during
builds, so this is scoped Windows evidence. All ten release gates remain open,
including original-engine/gameplay, cross-platform clean packages and human
accessibility acceptance. No unrelated defect was found in this increment.

Ordered external material authoring now shares `core/model_material_slots`
between the Surface review dialog, selected-slot assignment and `model slots`.
Add, replace, remove, reorder and clear retain intentional duplicate paths and
use normal candidate validation, cancellation, selection-aware history,
source/recovery, export and staging. Per-surface preview indices select images
without dirtying the source. GUI and `model materials --surface N
--material-slot N` share geometry-free preview snapshots and bounded resolution.
Embedded MDL skins keep their separate native controls. See
[Material Slots](MODEL_MATERIAL_SLOTS.md) for exact semantics and integration gaps.
The accepted Windows matrix passes twelve named optimized tests at 1x and four
GUI executions at 2x. The new service/CLI suite contributes 80 assertions for
ordered duplicates, strict arguments, path/count limits, exact source and
selection preservation, one-step undo, recovery, MD2/MD3 round trips, embedded
skin restrictions, request coalescing/cancellation, real CLI dispatch and
read-only alternate resolution. GUI tests cover the buffered review, worker
locking/cancellation, selected-slot assignment, independent per-surface preview
choices, material handoff and preview pixels. Twelve widget-owned renders were
reviewed across dark, both high-contrast themes, 200% text, expanded translations,
RTL and actual 1x/2x pixel ratios. Visual review caught Qt reversing the numeric
prefix in an RTL combo despite its widget direction; an explicit directional
mark and a Qt text-layout assertion now preserve the technical reading order.

Commands, diagnostics, source/binary hashes and renders are retained in
`.agents/tmp/modeller-rc/evidence/material-slots/milestone.json`. All 124 modeller
production files and tested binaries remained stable during accepted validation.
Other studio work changed the shared checkout, so this remains scoped Windows
evidence. The change adds no library or renderer and closes none of the ten
release gates above. Whole-surface selection/transforms are covered by the later
increment above; original engine/gameplay, clean packages, macOS/Linux and human accessibility acceptance
remain open. No unrelated defect was found in this increment.

Surface management now shares all-pose rename, face separation/movement,
duplication, joining and deletion between the Surface inspector and
`model surfaces`. Exact UVs, normals, poses, winding, surviving seam marks,
unused vertices and ordered material slots are retained. Move/Join refuse
mismatched bindings unless the user explicitly chooses target materials. Shared
boundary duplication checks global geometry/storage capacity before allocation;
candidate validation and cancellation precede one selection-aware undo step.
The workflow also provides a way to resolve native MD3 surface-name collisions.

Resulting faces hand directly to UV/geometry finishing; material previews follow
renumbered surfaces. Source saves, recovery, native exports, package staging and
level handoff use the ordinary document services. External `.skin` and other
name-based references still need separate dependency review. The original Join
checklist was transient; the later whole-surface workflow above supplies persistent
selection and transforms without changing component selection to span surfaces.
Alternate material editing now uses the shared slot workflow above. See
[Surface Authoring](MODEL_SURFACES.md) for exact semantics and CLI examples.

The accepted Windows matrix has ten named optimized tests at 1x and three GUI
executions at 2x, all passing. The new core/CLI suite contributes 118 assertions:
all source/target orderings, exact all-pose attributes, disappearing seam edges,
unused vertices, material mismatch/adoption, storage limits, cancellation,
history, source/recovery round trips, native MDL metadata, MD2/MD3 export and
actual CLI dispatch/protected writes. The GUI tests cover the reviewed operation,
mutation locking, cancellation, selection/UV handoff and texture pixels after
surface deletion and Undo. Twelve widget-owned renders were reviewed across
normal dark, both high-contrast themes, 200% text, expanded translations, RTL and
actual 1x/2x pixel ratios. Visual review caught clipped surface headings/names at
large text; contents-based column sizing and a regression assertion fixed it.

Exact commands, source/binary hashes, diagnostics and renders are retained in
`.agents/tmp/modeller-rc/evidence/surface-authoring/milestone.json`. The modeller
production files and tested binaries remained stable. Other studio work changed
the shared checkout, so this is scoped evidence rather than clean-package or
cross-platform acceptance. No OS capture or physical input injection was used.
All ten release gates remain open, including original-engine/gameplay and human
accessibility acceptance. No unrelated defect was found in this increment.

Explicit boundary-loop filling now closes chosen holes through the shared mesh
document, Geometry inspector and `model edit --operation fill-boundary-loops`.
One seed edge expands to its entire indexed boundary; a reference pose chooses
the cap and every animation pose must support it. Concave and suitably
projectable nonplanar loops retain existing positions, normals, UVs, seam marks,
materials, tags, collision and other surfaces. New cap faces become the selection
for finishing. All selected loops commit together through normal validation,
undo, recovery, source saves, native export and package/level handoff.

The accepted Windows matrix has ten named optimized modeller tests at 1x display
scale and three UI executions at 2x, all passing. The boundary service contributes
61 assertions covering independent volume/winding checks, full-source retention,
concave and collinear corners, nonplanarity, multiple holes, reference-pose
selection, later-pose collapse/intersection, coplanar overlaps, shallow crossings,
legal seam/partial-edge contact, a 1,024-vertex animated loop, bounded work,
cancellation, undo/recovery, MD3 export and real CLI writes/dry runs/protection.
The first multi-loop regressions exposed a false intersection at nearly coplanar
adjacent cap faces; deriving the intersection direction from plane-slice segments
fixed it without weakening the crossing/overlap controls.

The new UI fixture checks accessible role/name/description, worker responsiveness
and mutation locking, displayed-pose choice despite current-frame transform scope,
face-selection/UV handoff, undo/redo and cancellation. Six widget-owned renders
were reviewed across normal dark, both high-contrast themes, 200% text, expanded
labels, RTL and actual 1x/2x pixel ratios. No OS capture or physical input injection
was used. A separate pre-existing health-control inconsistency was fixed:
Collision mode now disables geometry health actions, matching Tags mode.
Generated help, credits, whitespace and all 21 translation-catalog checks pass.

Evidence is retained in
`.agents/tmp/modeller-rc/evidence/boundary-fill/milestone.json`, with exact commands,
source/binary hashes, diagnostics and widget renders. The application was rebuilt
after a concurrent CLI edit; the final modeller production sources and tested
binaries remained stable. The shared checkout still contains unrelated active
work, so this is scoped implementation evidence rather than clean-package or
cross-platform acceptance. Filling uses bounded, scale-relative floating-point
checks on proposed caps in the active surface; it does not scan pre-existing or
cross-surface intersections, infer all holes, optimize triangulations, or support
every strongly folded loop. UV/normal finishing, broader topology tools, complete
native-engine/gameplay evidence and human accessibility acceptance remain open.
All ten release gates above remain unchecked. See [Editable Meshes](MODEL_MESH.md)
for the exact operation limits and CLI behavior.

Quake III `.skin` files now have an explicit authoring import shared by the
Surface inspector and `model skin`. It validates complete surface coverage,
native name matching, bounded ASCII syntax and safe shader paths before changing
any primary material. Alternate slots and all geometry, UVs, poses, tags and
collision remain intact. Local files, exact staged-package occurrences and saved
package drafts use normal document validation, cancellation, undo, recovery and
output protection. Receipts identify the occurrence and report unused bindings
and ignored attachment markers. Material and UV previews resolve the new paths
through the existing package service.

The accepted Windows matrix contains 13 named modeller checks at 1x display
scale and three UI checks at 2x, all passing with the optimized warnings-as-errors
build. It covers malformed/oversized input, full-source preservation, one-step
undo/redo and no-op imports, verified reads, cancellation, stale package choices,
real CLI dry runs/writes, staged replacement bytes and independent MD3 shader
record inspection. The new UI fixture explicitly checks application-level RTL
for the picker and receipt, both high-contrast themes, 200% text, expanded labels,
native accessibility metadata and refreshed texture pixels. Widget-owned renders
were reviewed; no OS capture or physical input injection was used.

The scoped record is
`.agents/tmp/modeller-rc/evidence/skin-bindings/milestone.json`, with commands,
source/binary hashes, retained diagnostics and the final widget renders. The
initial texture assertion exposed an incomplete fake package reader; it was
corrected and the affected tests rerun. Unrelated package-view and audio settings
build errors were also repaired. Credits, generated help and translation
extraction checks pass; all 21 catalogs include the new strings. The shared
checkout remained active, so this is not clean-package or cross-platform release
evidence. Automatic runtime `.skin` selection, source-port extensions, full
shader rendering and physical keyboard/screen-reader acceptance remain open.
All ten release gates above remain unchecked.

Native export rendering now has a reproducible external-engine check in
`src/tests/model_render_workflow.py`, using an unmodified FTE client and an
original MenuQC scene in an EGL pbuffer with Mesa llvmpipe. The real studio CLI
builds original textures, two-pose MDL/MD2/MD3 models and a grouped MDL. Engine
screenshots verify front-face visibility, projected positions, asymmetric UV
colours, both poses, the midpoint blend and discrete grouped-frame timing.
The workflow uses unique model stems, disables FTE's automatic replacement
models and verifies native frame identities, so MD3 cannot silently stand in
for an MDL/MD2 export.

The four cases complete 18 recorded steps and produce 20 engine-written images.
All 15 positive panel checks and 44 engine identity/timing checks pass. Three
deliberate controls each fail exactly five matching panel checks: reversed MD2
faces are culled, flipped MD3 V changes all four texture quadrants, and an MDL
top-left colour seed exposes GLQuake-style skin flood-fill. The other 30 panels
in those controls pass. The fixture leaves flood-fill enabled; studio exports
preserve indexed pixels and previews do not emulate that engine preprocessing.
Authoring guidance now explains the required separation between background
padding and used UV regions. The final MenuQC compilation has no warnings.

Evidence is retained in
`.agents/tmp/modeller-rc/evidence/engine-render/milestone.json`, including
fixture/tool/source hashes, per-pixel observations, commands and engine images.
The optimized studio executable and modeller C++ inputs match the preceding
winding/server milestone; this increment changes test fixtures and documentation,
not production C++. It adds no studio graphics dependency. No desktop window,
physical input control or OS capture is used. Original-engine gameplay, complete
material/skin/attachment rendering and cross-platform release acceptance remain
open. All ten release gates remain open. See
[Model Engine Acceptance](MODEL_ENGINE_ACCEPTANCE.md#offscreen-native-rendering)
for reproduction, engine findings and exact scope.

The native winding/server milestone below is retained as prior evidence.

Native MDL/MD2/MD3 import and export now convert between the engines' clockwise
front faces and the editor/OBJ counter-clockwise convention. Both MD2 render
streams agree; stored UVs, normals, seams and poses retain their identities.
An independent byte-layout regression checks animated export, reimport,
normal rebuilding and OBJ handoff. Existing editable files retain their recorded
triangles; earlier experimental native imports/exports need orientation review.
See [Model Engine Acceptance](MODEL_ENGINE_ACCEPTANCE.md) for that compatibility
guidance and the reproducible optional server workflow.

The generated workflow completes 25 recorded steps, including the nested 21-step
compiler proof and five FTE dedicated-server runs, without client/input control.
Each run executes 36 assertions. On all three BSP formats, pose/front/back-face
traces and player collision/movement checks pass. The two negative controls
detect a damaged MD2 pose and missing collision. FTE's rotated MD3 tag lookup
reports the opposite forward axis: each positive run therefore records 35 passes
and one engine finding, not a clean pass. Independent MD3 byte checks retain
Quake III's expected basis and authored frame names. The fixture also records
FTE's synthesized frame names and FTEQCC's minimal-system-definition diagnostic.

The recorded optimized Windows build passes 21 suites at 1× and six
editor/viewport/worker suites at 2× (27 executions). Modeller C++ inputs and
tested executables match the build across the final runs. The first 2× group
also passed, but concurrent source additions interrupted its evidence snapshot;
bounded snapshot retries and earlier result persistence allowed a complete
repeat. Guide, credits and translation-source validation pass.

Evidence is retained in
`.agents/tmp/modeller-rc/evidence/engine-acceptance/milestone.json`, with external tool source
inventory/build records in `.agents/tmp/modeller-rc/engine-tools/`. The server
loads Quake II/III BSPs under the generated QuakeC fixture; it does not establish
original game-DLL/VM behavior, client rendering/input, package readiness or
cross-platform release acceptance. All ten release gates remain open.

The collision manipulation milestone below is retained as historical evidence.

Collision selection now integrates viewport edges, component-table rows and the
inspector list. Shared candidate transforms move and rotate in world axes and
scale in each oriented box's local axes, with snapping, pivot choice, bounds
checks, cancelled/invalid preview rejection and one undo step per gesture.
Geometry's numeric transform and `model collision --operation transform` use
the same service. Collision remains static across poses; export and level
placement consume the transformed volumes. Validation for this increment is
recorded separately in
`.agents/tmp/modeller-rc/evidence/collision-manipulation/milestone.json`.

The final optimized Windows build passes 13 suites at 1× and six GUI/viewport
suites at 2× (19 executions), with unchanged modeller sources and executables
through both runs. A 64-case independent corner oracle covers composed rotations,
local nonuniform/mirrored scale, external pivots and gimbal singularities. The
same executable passes the 21-step real-compiler workflow, including a box
transform before Quake/Quake II/Quake III BSP hull/brush inspection. Widget-owned
renders cover dark and both high-contrast themes, 200% text, expanded RTL and both
display scales. Visual review corrected a wide table heading and overlapping
expanded gizmo labels. Shared shell changes are recorded separately; this is
scoped verification in an active checkout, not a frozen release package.

Concurrent translation extraction left stray XML endings in four catalogs.
Only the extra tails were removed after verifying complete documents and retained
messages; catalog checks now pass. Serializing concurrent catalog writers remains
an unrelated tooling issue. Automatic approval review blocked removal of the
generated failed-test folder `runtime/collision-ui-wkNXoO`; it remains under
`.agents/tmp/modeller-rc/`, with the refusal recorded in the milestone.

The earlier static-box authoring/compiler milestone is retained below as
historical evidence.

Static collision authoring now uses up to 64 independently oriented boxes in
the mesh document. The Collision inspector supports numeric edits, fitting to
selected components or whole-model poses, rename, duplicate and delete. Solid
selected edges and dashed unselected edges share the completed viewport camera
snapshot; framing includes visible boxes. Exclusive selection, document
fingerprints, bounded history, source schema 5 and local recovery retain the
volumes. MDL/MD2/MD3/OBJ exports and assembly baking disclose omitted collision.

`model collision` shares authoring and validation with the GUI, including text
and JSON inspection, strict options, locale arguments, dry runs and guarded
source/derivative writes. Separate Quake-family map exports use shared convex
brush generation. Placement uses the level worker and guarded commit, respects
active scene destinations and locks, and forms one level undo step. Numeric map
flags supply Quake II player clipping; Quake III requires an explicit project
shader. Shader availability in the user's project remains unverified.

The generated compiler workflow exercises source import, collision authoring,
texture export, map export and placement, then runs real ericw-tools and q3map2.
Independent BSP readers verify empty Quake draw-hull points and solid expanded
hulls at the boxes, Quake II/III player-clip contents, leaf references, rotated
plane membership and absence of visible clip faces. It uses original assets,
preserves source inputs and refuses leaks or compiler warnings. No game was
launched; engine movement acceptance remains open. See
[Model Collision](MODEL_COLLISION.md) for controls, CLI, format limits and the
reproducible optional compiler proof.

Collision boxes are static and independent of rendered props, animation and
attachments. Arbitrary convex editing, animated hitboxes,
linked placement updates, native keyboard/screen-reader acceptance and clean
release packages across Windows/macOS/Linux remain open. This increment does
not close any of the ten release gates.

The final recorded optimized Windows build passes 16 suites at 1×, including
the collision core/CLI/UI tests, existing modeller regressions and an audio
session check for a shared-build repair. Five viewport/worker/GUI suites also
pass at 2×. The same executable completes the 20-step compiler proof for all
three targets. Collision captures cover standard dark, both high-contrast
themes, 200% text, expanded RTL and both display scales. No modeller source or
executable changes during these final runs; concurrent shell/workspace changes
are recorded separately. Translation, offline-guide and credits checks pass.

Evidence is retained in
`.agents/tmp/modeller-rc/evidence/collision/milestone.json`, with source/binary
hashes, sanitized test results, widget-owned renders and generated compiler
fixtures. This is verification of a recorded shared-checkout build, not a
frozen release package. Concurrent package-plan changes and audio integration
temporarily broke earlier builds. Two missing audio translation wrappers were
restored; the completed audio CLI Meson registration was picked up by the
successful rebuild. The audio session suite passes. Earlier preliminary runs
and fixture corrections remain identifiable in the evidence record.

Evidence below is retained from earlier increments. Its gap lists describe
the state of those tested builds.

Assembly recovery now protects the linked recipe directly. Applied edits queue
one atomic background checkpoint, with one replaceable pending snapshot; dirty
selection and completed timeline position are also checked every five seconds.
The existing mesh recovery preference controls it. Copies carry a strict,
checksummed recipe/context payload and original-source provenance. Storage is
bounded to 32 records and 32 MiB; scans inspect at most 128 entries and 32 MiB.
Live Qt session leases prevent another editor or CLI discard from deleting an
active copy. Full storage and invalid copies are reported without automatic
pruning. Save, approved discard and clean undo retire only the current session's
copy after its writer finishes; disabling recovery retains existing copies.

The recovery chooser and `model assembly --operation recoveries|recover|discard`
share verification and reviewed-digest checks. Restoration preserves the reviewed
copy and opens an unsaved draft with its selected part and time. Original sources
remain protected even with overwrite enabled. Empty recipes remain dirty, and
unavailable model/package references remain repairable. Package references use
the current package context. Recovery does not contain model/package bytes,
unapplied inspector text or undo history.

Windows release tests cover coalescing, retirement races, failed writes,
corruption, stale reviewed digests, quotas, cancellation, source preservation,
CLI parity and GUI context restoration. A separate helper process is terminated
after staging a replacement and before commit: the preceding valid checkpoint
survives, and Qt reclaims the dead process's session/writer locks. Widget-owned
renders cover standard dark, both high-contrast themes, 200% text, expanded RTL
and 1×/2× display scaling. Visual review found horizontal recovery-row overflow;
wrapping rows and a layout assertion now prevent it. Native keyboard and screen
reader acceptance, maximum-assembly latency, engine acceptance and clean release
packages on all three platforms remain open. No release gate is closed here.

A repeated GUI run exposed contention between retirement of the previous copy
and the next document's checkpoint. Short-lived storage locks now wait on the
worker for at most one second per lock, with cancellation for checkpoint/discard
work. Live editor leases still refuse immediately. A deterministic held-lock
fixture verifies later publication and cancellation without changing the prior
copy; repeated UI runs cover rapid save/edit/restore/discard transitions.

The final recorded build passes 12 suites at 1× and five worker/UI/shell suites
at 2×, followed by 15 successful repetitions of assembly recovery UI transitions.
No source or executable changed during those three test runs. Assembly sources
still match the compiled fingerprints; concurrent shared-package and level work
is recorded separately. All 21 translation catalogs validate, and offline-guide
and credits checks pass.

Evidence for this increment is recorded in
`.agents/tmp/modeller-rc/evidence/assembly-recovery/milestone.json`. The shared
checkout continues to change, so source and executable fingerprints delimit the
tested builds. Unrelated UDMF integration compile errors were corrected
with targeted command-registration and selection-selector fixes. A concurrent
package-index refactor left one session call using a removed helper name; it
now calls the shared directory-index API. An initial
worker-suite failure concerned saving evidence into a missing capture folder;
the audit helper now creates and validates its capture destinations.

The linked-assembly milestone adds **Models > Assemble**, a schema-1
`.assembly.json` document and matching `model assembly` CLI. File or current
package references retain their own frame ranges, rate, phase, looping and
interpolation settings. The shared sampler composes nested tag bases and
uniform scale, handles reflected winding, and refuses invalid interpolation.
Selection-aware undo, branch removal, relative source references, source hash
checks and input/package protection use normal document/write services.
Resolution and sampling run on the common cancellable worker. Missing inputs
leave the recipe repairable with no stale preview; cancellation leaves the
previous recipe, selection and preview unchanged.

An explicit static bake enters the existing mesh editor for source/recovery,
UV/topology edits, package staging, dependency review and level placement.
Direct OBJ/MD2/MD3 export uses the same sampled pose and guarded writer. Both
surfaces explain the loss of hierarchy, independent animation, tags and native
skin metadata. Input models remain unchanged. Package context refreshes with
staged revisions and palette changes; material previews share the existing
resolver. **Details** exposes dependency hashes, samples and material diagnostics.
See [Model Assemblies](MODEL_ASSEMBLY.md) for the exact format, CLI and bounds.

The optimized Windows x64 build passes seven final suites at 1×: assembly core,
CLI and UI, document worker, shell integration, mesh editor UI and material UI.
The five worker/UI/shell suites also pass at 2×. There are no source or executable
changes during either final test run. Tests cover nested transform oracles,
independent timing, reflected tags, malformed/cyclic graphs, cancellation,
selection history, relative saves, stale source rejection, input protection,
portable package drafts, native bake output and OBJ reimport. UI tests additionally exercise
missing-source repair, staging deletion, reduced-motion/manual sampling,
accessible control names, and close cancellation with a deferred continuation.

Widget-owned renders cover standard dark and expanded RTL at 200% high-contrast
light; the tests also visit high-contrast dark. Visual review found horizontal
inspector clipping, which was fixed with stacked axis controls, a wrapping form,
an appropriately sized inspector and a two-row timeline. The regression checks
both horizontal scroll range and wrapped-label geometry. No user input was
injected and no operating-system capture was used. Playback waits for the
current viewport render before replacing its mesh, sampling elapsed time so
slower rendering skips obsolete poses.

Evidence lives in `.agents/tmp/modeller-rc/evidence/assembly/milestone.json`,
with per-run source/executable hashes and sanitized test results. Shared package
API changes temporarily required rebuilding stale objects; the later build and
tests pass. Two intermediate evidence snapshots raced documentation writes after
successful compiles, so those incomplete captures are not used as the final
production build record. The final production build has no source changes during
its recorded run. A later playback assertion also passes at both scales without
mutating the recipe; the milestone records those two additional UI executions.
This remains scoped verification in a shared checkout, not a frozen release.
All 21 translation catalogs were refreshed, and offline-guide/credits checks pass.

This milestone supplies basic linked-assembly authoring; older references below
to unimplemented child assembly are superseded by this bounded workflow.
Animated assembly export, native game configuration,
collision authoring, original-engine acceptance and maximum-assembly performance
remain open. The assembly-recovery increment above supersedes this milestone's
earlier recovery gap; mesh recovery still protects the separate baked document.
Physical keyboard/screen-reader acceptance and clean Windows/macOS/Linux
distributions still require evidence. None of the ten release gates above is
closed by this milestone.

An optimized Windows x64 audit now complements the debug measurements below.
It uses a separate Meson `release` build, clang-cl 20.1.7, warnings as errors,
the release CRT and Qt 6.10.1 release libraries. The earlier build used both
application and Qt debug code. This comparison does not isolate the cause of
the earlier run-to-run variance.

Three sequential viewport runs and three editor runs at each of 1× and 2× scale,
plus final replays after rebuilding, all pass their configured 200/300 ms
event-loop budgets and 2-second completed wireframe budget. Across these sixteen
runs, the largest observed UI gap is 21.30 ms. Selecting all faces completes its
release image in 61–79 ms at 1×; the fully selected wireframe completes in
367–393 ms at 1× and 615–704 ms at 2×. Document preparation remains
asynchronous; these are wall-clock measurements from offscreen Qt widgets on
one shared host, not portable latency guarantees.

`model-document-latency-smoke` adds a valid maximum-geometry fixture to the
production document worker and recovery writer. It verifies a complete source
fingerprint after save/reopen; all poses and dense edge selection after restoring
an unsaved recovery draft; the selected pose and geometry counts after OBJ
export/reimport; and unchanged source bytes, selection and revision after
cancelled serialization. It records completion times separately from event-loop
gaps, supports the optional `VIBESTUDIO_MODELLER_MAX_DOCUMENT_GAP_MS` budget,
and keeps all fixture files in an automatically removed test directory.
The source is 25,798,374 bytes, the recovery payload envelope is 28,471,880 bytes,
and the OBJ frame is 12,613,830 bytes. The 64 MiB serialized limit remains separate
from the geometry limits; these simple coordinates do not represent the largest
possible JSON encoding.

Nine focused suites pass at 1× after the final rebuild; viewport, editor and
document latency checks also pass at 2×. The final document checks measure:

| Worker operation | 1× completion | 2× completion |
| --- | ---: | ---: |
| Save source | 1.31 s | 1.41 s |
| Reopen source | 1.27 s | 1.48 s |
| Write recovery | 2.61 s | 3.86 s |
| Restore recovery | 2.11 s | 3.09 s |
| Export OBJ frame | 248 ms | 330 ms |
| Verify OBJ reimport | 366 ms | 559 ms |
| Cancel source serialization | 182 ms | 247 ms |

The largest event-loop gaps in those two document checks are 8.15 and 10.45 ms.
Earlier complete service checks took up to 3.14 seconds to save and 5.91 seconds
to write recovery. File throughput varies on this host even though the UI stays
responsive; these times are observations, not acceptance limits for every disk
or machine. Initial export-harness failure came from using the native browser
API for an editable source; the corrected test uses `exportEditableModel`, the
service behind the mesh editor and `model build`.

The optimized evidence, sanitized results and source/executable hashes are in
`.agents/tmp/modeller-rc/evidence/release-performance/`. These focused builds
use the canonical Meson release options but are not packaged release builds.
The shared checkout continued changing in other modules; each build and test
record identifies source changes and the exact executables used. This closes
the absence of optimized evidence for this grid, while varied materials and
surfaces, translucent overlap, complete UV and package workflows, original-engine
acceptance, physical assistive-technology review and native macOS/Linux
performance remain open. None of the required workflow boxes is closed by
these measurements alone.

The component-authoring audit uses a valid 65,536-vertex, 130,050-triangle,
16-pose grid (1,048,576 stored frame vertices). It exercises real editor
preparation, face/vertex/edge mode switches, Select All, exact vertex queries,
orbit and pose changes. Table resets, selection range counts and current-pose
cell values are checked alongside event-loop gaps and completed-image latency.
The final harness applies the production dark theme at standard density and
records its theme and device scale; initial investigations used Qt's default
theme.
The initial editor preparation took 41 seconds; selecting every face took
1.6 seconds. A 90-second process watchdog stopped that baseline run during the
following frame refresh.
Later measurements exposed multi-second vertex-marker/picking and edge-table
stalls. The audit's baseline and follow-up records live in
`.agents/tmp/modeller-rc/evidence/authoring-latency/`.

Internal revision hashing now streams geometry rather than allocating a JSON
tree for each fingerprint. Documents prepare shared edge order/incidence indexes,
complete edge-selection sets and their connected vertices, with cancellation
and conservative history accounting. Component tables reuse these sets and
retain their rows and compact selection across pose/selection refreshes. Header display uses
separate selection state, bounded initial sizing, retained user widths and
full-value tooltips; unchanged inspector refreshes preserve the completed preview. Vertex projections,
occlusion, a conservative exact-pick index and CPU marker images prepare on the
existing worker. Marker-only changes reuse the mesh image. Physical-pixel square
and dotted stamps retain visibility and selection cues at fractional scaling.
Gizmo queries reuse validated selection bounds. Oblique silhouette vertices
require connected-face coverage within the marker footprint, preserving
occlusion without losing acute corners to half-open pixel sampling.

`model-fingerprint-smoke` checks serialized field identity, native indexed MDL
metadata, deterministic seam ordering, signed-zero equivalence and cancellation.
`model-vertex-overlay-smoke` compares 800 indexed queries against an exhaustive
picker and checks marker shape, visibility, scaling, bounds and cancellation.
Dense-selection fixtures also verify that overlapping outlines cannot erase
selected centres and that vertex ordering does not change the resulting image.
Viewport fixtures cover occlusion, X-ray, surface changes, near-plane rejection,
replacement geometry and current-pose picks. Topology fixtures check cache reuse,
edge validation, edits, cancellation and undo/redo. Source/recovery schemas and
external-file hashes are unchanged; no new dependency or imported code is used.

The Windows x64 debug regression run passed 51 behavior suites. Its authoring
timing check exceeded the optional 200 ms UI budget. After the complete-selection
cache and marker corrections, ten focused 1× checks passed; the authoring check
again exceeded that budget, reaching 647 ms. The same executable's final diagnostic
replay passed at 150 ms. At 2×, the authoring check passed the 300 ms budget at
239 ms; the viewport check peaked at 118 ms and completed all-face wireframe in
1.56 seconds. These differing results keep timing stability open for profiling
and optimized release acceptance; they are not portable response-time guarantees.

The final diagnostic 1× run prepared the maximum document in 1.57 seconds,
selected all 195,585 edges in 17 ms, and executed 16 vertex queries in 0.27 ms.
At 2× those calls took 1.43 seconds, 19 ms and 0.40 ms respectively. Preparation
uses the document worker, so its call duration is distinct from UI delay.
The extra complete-selection sets add approximately 7.7 MiB to the conservative
history estimate for this grid; shared allocations are not a process-memory
measurement.

Eleven focused suites pass at an asserted 2× scale across the recorded run and
replay. The initial MDL UI run reached its final contrast scenario before its
90-second watchdog expired; an unchanged-executable replay allowed 180 seconds
and passed in 41 seconds. The marker fixtures check fractional scaling directly.
Dense-authoring captures are 1,600×1,000 and 3,200×2,000 pixels; older editor layout
fixtures still capture a logical-size canvas even when the widget uses 2× scaling.
Visual review verifies retained dense-selection colour, all four oblique plane
corners, table context, and enlarged/expanded RTL controls.

`authoring-latency/milestone.json` indexes the sanitized results, captures and
source/executable hashes. `tested-build.json`, `marker-build.json` and
`acceptance-build.json` distinguish the broad regression and follow-up builds.
The shared Meson file changed during the latter builds; modeller sources and the
final tested executables are checked separately. The root Meson file also
changed after the acceptance build. This remains an active shared checkout,
not a frozen release tree.

That debug audit did not establish maximum-document JSON save/recovery/export
latency; the optimized service audit above now covers the grid. The broader
scene, integration, accessibility and platform gaps listed above remain open.

Wireframe now uses an original CPU line rasterizer on the existing worker. It
integrates stroke coverage over pixels, deduplicates shared surface edges and
draws selected dashes after ordinary edges. Logical widths scale with display
density and high-visibility mode. Near-plane cuts, original selected edges and
displaced faces retain their respective geometry; no decimation is used.
Offscreen coordinates cannot enlarge the scan beyond the bounded image, and
cancellation discards partial output. The visible rendering/error status wraps
and grows with translated text and the current font.

`model-wireframe-smoke` checks stroke area against independently drawn polygons
at 16 angles, five widths and three display scales (240 comparisons). It also
checks reversal, dash order, subpixel coverage, alpha, extreme clipping, invalid
values, the shared image ceiling and cancellation. Viewport tests exercise a
near-clipped selected edge, shared-edge selection order and high-visibility
output. The maximum-grid harness now includes selecting all 130,050 faces.
Wireframe evidence is retained under `.agents/tmp/modeller-rc/evidence/wireframe/`.

The wireframe Windows x64 debug build passes 17 regression suites, including
the shared Doom/Levels renderer, and six viewport/editor suites at an asserted
2× display scale. Follow-up fixture validation and performance-budget tests pass
at both scales. The final maximum scene completes ordinary wireframe images in
0.36–0.38 seconds at 1× and 0.54–0.59 seconds at 2×, compared with the preceding
2× baseline of 3.5–4.2 seconds. Selecting every face takes 0.63 seconds at 1× and
1.59 seconds at 2×. All three wireframe stages pass the optional local two-second
completed-image budget. Maximum event-loop gaps across the final performance
run are 125 ms at 1× and 164 ms at 2×, within the local 200/300 ms budgets.
These are measured debug-build results, not portable frame-rate guarantees;
optimized release builds and broader scene combinations still need acceptance.

Viewport-owned images verify clipping, selected shared edges, high visibility,
200% text and expanded RTL status. The 2× wireframe captures have twice the
physical dimensions of their 1× counterparts. `tested-build.json`,
`fixture-build.json` and `budget-build.json` identify the regression and two
test-only follow-up builds; renderer source hashes remain unchanged between
them. Sanitized results and final latency records are indexed by
`wireframe/milestone.json`. Shared work continues elsewhere in the checkout;
these results apply to the recorded executables, not a frozen repository.

Viewport projection, clipped-triangle preparation, picking-index construction
and wireframe painting now run on the existing cancellable render worker.
Value snapshots preserve frame interpolation, edit transforms, per-surface
textures, selected edges and near-plane clipping. Camera/pose revision checks
reject stale picks; attachment markers follow the displayed image. A bounded
conservative screen index keeps exact depth, subpixel coverage and texture-alpha
picking while avoiding a whole-scene allocation on each query. Selection and
style updates reuse unchanged projection/index data. The shared Levels preview
uses the same path; this adds no dependency or rendering backend.
Dense-grid image inspection also exposed fractional shared-edge cracks.
Canonical edge equations now make reversed edges exact opposites; a translucent
grid regression rejects both background gaps and double-blended seams.

An original, validated 256×256 grid reaches 65,536 vertices and 1,048,576 frame
vertices across 16 distinct poses, with 130,050 nondegenerate triangles. The
Windows x64 debug baseline had approximately 278 ms maximum event-loop gaps for
orbit, 1,094 ms for wireframe, and 2,510 ms of synchronous work for 16 picks.
The first worker/index run reduced those to approximately 18 ms, 16 ms, and
2 ms respectively. These are local measurements, not portable frame-rate
guarantees. The harness records both UI gaps and time to completed images;
source/build evidence and subsequent runs live under
`.agents/tmp/modeller-rc/evidence/viewport-latency/`.

The preceding viewport/index Windows debug build passed 16 focused suites, including native browser
and frame export, editable documents, MDL controls, animation, UVs, attachments,
transforms, the shared Doom/Levels preview, raster correctness and maximum-grid
latency. Six viewport/editor suites also pass at an asserted 2× device pixel
ratio, including existing enlarged-text, contrast and translation-expansion
fixtures. The final maximum event-loop gap is 171 ms at 1× and 122 ms at 2×,
within the explicit local 200/300 ms budgets. At 1×, orbit takes about 17 ms
of maximum UI delay, wireframe 20 ms, and 16 picks about 2 ms of synchronous
work. These are UI-response measurements: completing the dense 2× wireframe
image still took 3.5–4.2 seconds in that debug run, motivating the dedicated
wireframe path above.
`tested-build.json` records the tested renderer sources and all 16 executables;
`tests-final.json` and `tests-2x.json` retain sanitized results.

The viewport-only test does not establish full authoring performance. The newer
component-authoring audit covers table refresh, selection and vertex overlays;
document serialization, maximum surface/texture combinations, worst-case transparent
overlap, original-engine acceptance, physical assistive-technology review and
cross-platform release gates remain open independently of this improvement.

Native package previews now share `app/model_preview_worker` with OBJ. MDL,
MD2 and MD3 geometry, skin/material lookup and native header analysis run off
the UI thread using immutable snapshots. MDC/MDR/IQM retain metadata-only
behavior. `core/model_archive` gives model and palette reads a shared 64 MiB
bound, exact entry identity and final payload verification; native decoding
checks cancellation inside pixel, triangle, vertex, tag and bounds loops.
MDL's frame-vertex cap includes expanded groups, and MD3's cap includes every
surface and padded pose. Cancelled decodes publish no partial model.

The browser retains separate surface images, alternate native skin extensions,
MDL indices/palette/groups, MD3 attachment poses and raw metadata. Its status
strip now follows asynchronous loading/cancellation/failure transitions. Export
requires finished geometry, and selected metadata-only or failed models cannot
silently open as a starter cube. Existing mesh documents remain accessible while
another browser preview loads. Source and package files stay unchanged.

Native-preview build logs, sanitized test results, source/binary fingerprints
and Qt widget renders are retained under
`.agents/tmp/modeller-rc/evidence/native-preview/`. The focused fixtures exercise
decode/bounds cancellation, aggregate allocation caps, bad/ambiguous/changed
package reads, exact palette and group retention, request replacement, worker
shutdown, browser Cancel/retry, stored poses and the native editor handoff.
These changes do not complete the maximum-scene viewport/raster audit, original
engine acceptance, human assistive-technology review or cross-platform release
packaging. No new dependency or imported code was added.

Windows x64 debug evidence covers 16 distinct focused suites across the recorded
builds. The final native browser and viewport suites pass after the status-strip
and scalable empty-state corrections. Native browser checks also pass at an
asserted 2x device pixel ratio with 200% text, expanded labels, high-contrast
light, RTL and reduced motion. Current renders are in `layout-1x/` and
`layout-2x/`; narrow inspector/list columns still elide long labels and remain
resizable. Physical keyboard and screen-reader validation is still required.
CLI documentation matches 195 registered commands, and all 21 translation
catalogs parse and contain the final preview messages. The offline guide is
current. `milestone.json` distinguishes the broader regression run from the
final layout build and records shared-source changes; the checkout is not frozen.

Shared-worktree validation also caught missing Code save-path includes and
undocumented compiler-workspace commands; the final build and CLI audit pass
with those corrections. Four catalogs had a duplicated empty-translation tail
after their XML root. Only that verified redundant tail was removed before a
successful catalog refresh; existing translations were preserved.

Polygonal OBJ intake now shares the editor, package browser and CLI model
services. It retains independent UV/normal corners, winding, smoothing groups
and direct package material references; bounded planar polygons triangulate
without losing corners. Malformed indices, unsupported records, loose geometry,
non-planar/intersecting polygons and excessive parsing/geometry work fail before
adoption. The ordinary source, undo, material, native-export and staging paths
remain authoritative. Empty material assignments now clear preceding OBJ export
state, fixing an exported unassigned surface inheriting another material.

OBJ package preview runs verified bounded streaming, geometry decoding and
per-surface image resolution on one worker with a replaceable request. Selection
or package changes retire old work. Cancel, retry, close and late verification
failures cannot publish a stale mesh. Native browser previews now use the same
worker as described above. The OBJ tests include independent polygon
area/winding/interior oracles, UV and normal seams, unsupported-data failures,
limits, cancellation, a 24,000-vertex fixture, editable/OBJ/MD3 round trips, CLI
dry runs and source preservation, and the real saved-package/editor handoff.
Normal/high-visibility/expanded/RTL widget renders verify both material colours.
The browser inspector marks each resolved skin and avoids a false missing-skin
warning when per-surface images are ready.

Windows x64 debug validation passed all 46 modeller/image/material/package
regression suites. After the inspector status correction, the OBJ UI and native
shell suites passed again; OBJ UI also passed at actual 2x display scale. Its five
widget renders have exactly twice the baseline pixel dimensions, including
200% text, both high-visibility themes and expanded/RTL labels. CLI documentation
validation passed for 192 registered commands; extraction coverage passed for
all 21 translation catalogs. Offline-guide and credits checks passed. Scoped
source and executable hashes distinguish the broad run from the final UI fix.
Shared shell, CLI and Meson sources changed again during verification; executable
hashes stayed stable. These results identify the recorded builds and do not claim
validation of the later shared edits or a frozen checkout.
Evidence is recorded in `.agents/tmp/modeller-rc/evidence/obj/milestone.json`.

This is bounded polygon interchange, not full Wavefront scene/material support.
MTL shading conversion, free-form geometry and vertex colours remain open. Other
remaining gates include attached assemblies, collision authoring, broader
topology and UV tools, original-engine acceptance, maximum-scene responsiveness,
human assistive-technology checks and clean Windows/macOS/Linux distributions.
The older browser/`model export` OBJ write path now uses the shared guarded model
file service through `core/model_frame_export`. Destination review precedes
serialization; source/package/draft protection, explicit overwrite, competing
writers, checked writes and commit-time identity checks are shared by GUI and
CLI. No direct truncating write remains in those routes. Dry runs check the same
protections without creating output directories or files; text stdout retains
its platform line endings and carries no progress/omission prose. Omission notes
go to stderr alongside raw OBJ, or into JSON/activity details. The geometry-only
interchange format is unchanged; per-surface material export remains in the mesh
editor's authoring path.

The browser captures immutable mesh/frame/package values and uses the existing
cancellable document worker. Selection changes do not retarget a running export;
reentry is blocked, and studio close cancels then resumes after worker shutdown.
The actual Export OBJ button/file-picker flow also checks that Cancel produces
neither an output nor a second, misleading export-failure dialog.
Native loose-file CLI reading now shares the 64 MiB bound; malformed frame values
are usage errors. CLI package intake also reads the current portable draft plan.
Windows evidence for this change is kept under
`.agents/tmp/modeller-rc/evidence/frame-export/`. It covers source/manifest
preservation, serialization-time destination races, cancellation before and after
commit, overwrite and stdout contracts, archived/folder/draft inputs, the real
browser lifecycle and the shared progress UI at 2x display scale. Full-platform,
engine and worst-case responsiveness gates remain open.
Seven focused suites have passing latest results on Windows x64 debug, with
additional 2x runs of browser export and the shared document progress surface.
The final CLI binary also passed the archive/draft/loose-file export cases and
documentation validation for 193 registered commands. Extraction coverage passed
for 21 translation catalogs, and the generated offline guide is current. Source
and binary snapshots record the tested builds and later shared-worktree changes;
these checks do not imply a frozen checkout or cross-platform release approval.

The modeller now copies indexed skins directly from the current staged package.
Import Package Texture selects an exact entry and adds a slot, replaces the
selected member or appends with its duration. The metadata-only picker filters
paths without reading payloads; changed package revisions invalidate open
selections. Immutable snapshots, bounded verified reads, native image decoding
and candidate edits run through the normal cancellable document worker. CLI
skin operations share the service and accept archives, folders and portable
`.vibepackage` drafts. Outputs cannot replace their package/draft inputs.

Indexed PNG, PCX, Quake LMP/miptextures, WAL and M8 preserve their actual source
indices. Embedded palettes are authoritative; external palettes use strict
package lookup, then the model fallback only when candidates are absent.
Dimension/palette mismatches, ambiguous names, corrupt palettes, transparent
images, truncated mips, failed verification and cancellation leave the model
unchanged. Receipts identify the occurrence, byte count/hash and palette origin.
The Texture Editor's real staging service and planned-package adapter are covered,
including WAD2 lumps and unchanged package history. Import is a copy with one
model undo step; it does not create a live texture binding.

Windows x64 debug validation covers 44 unique suites across the modeller and
shared image/package services. The broad run passed 43 suites; the new handoff
fixture used mismatched WAD2 internal/lump names. Correcting that fixture and
rebuilding passed all five follow-up suites: skin source core/UI, native MDL
CLI/UI and the studio shell. Earlier fixture corrections distinguish WAL export
conversion from exact intake and use Qt's item-selection model under RTL.
The picker has semantic accessibility/focus and widget-render checks in dark
and both high-contrast themes, 100%/200% text, RTL and expanded labels. Both the
picker and native MDL inspector checks pass at an asserted actual 2× device
pixel ratio. Final palette diagnostics identify the failing palette path and
avoid suggesting an unavailable palette-index selector; the core/UI handoff and
MDL CLI suites pass again on that rebuilt implementation.

Builds, sanitized results, source/binary fingerprints and renders are retained in
`.agents/tmp/modeller-rc/evidence/mdl-skin-source/milestone.json`. Shared shell
and package-preview code changed during this work and was incorporated in the
follow-up build. This is evidence for the recorded binaries, not a frozen release
checkout. Later rebuilds briefly caught an incomplete level-placement helper and
an unrouted map-paste command in shared edits; those builds succeeded once their
implementations were complete. The newly registered `map paste` also needed its
CLI strategy entry, which was added to keep the shared documentation check current.
No additional dependency, renderer, schema or upstream code was added.
Original-engine acceptance, physical keyboard/screen-reader testing and clean
Windows/macOS/Linux release packages remain open.

Native MDL preview now shares a deterministic sampler between the editor and
`model mdl --time`. Stored timing follows separate cumulative pose/skin cycles;
original GLQuake timing follows the first pose interval and four skin texture
slots at 10 Hz. Tick conversion precedes slot wrapping, preserving boundaries
such as 0.6 seconds through repeated cycles. Native frame and skin selection
are independent, so a static pose can have an animated skin. An explicit entity
phase applies only to random-sync
software models. Exact stored poses, skin members and cycle lengths are available
through the CLI; invalid time, group and skin inputs fail atomically.

The ordinary transport plays, pauses and resumes native time with a monotonic
clock. Seeking remains available under reduced motion. Clip FPS and smoothing
disable during native preview; selecting an ordinary pose/clip or Use Clip Timing
returns to clip transport. Selected skin images prepare on the cancellable
document worker into at most 64 MiB of opaque, shared pixels. Model and UV raster
updates coalesce; ticks do not decode images or change source/history/recovery.
Document edits retire the prepared cache. Original-engine floating-point bugs,
player colours, fullbright lighting and model-flag effects are not emulated.
Transport and reduced-motion changes refresh the native status immediately,
including when the current pose and skin member have not changed.

The initial Windows x64 debug regression passes all 36 modeller suites and four
shared level material/package suites. A later follow-up passes ten affected
suites after adding stricter UV assertions and immediate transport status.
The MDL UI check passes three consecutive runs; the MDL UI and native
sampler checks also pass at an asserted actual 2× device pixel ratio. Semantic
controls and widget renders cover dark and both high-contrast themes,
100%/200% text, RTL, expanded labels, native texture changes, and unchanged UV
camera position/scale and source. These checks do not replace physical keyboard
or screen-reader acceptance. The final GLQuake tick-boundary correction passes
all three native MDL suites on rebuilt executables and the sampler suite again
at 2×, including exact ticks across groups of one through seven skin members.

The UV follow-up initially measured a canvas while queued material-row layout
changes were still completing. Geometry diagnostics identified the resize;
the test now settles that work before measuring a skin-only update and keeps
exact camera assertions. A shared level-code link failure during concurrent
edits resolved after rebuilding the changed implementation. The final boundary
follow-up retained identical hashes for all four executables and 116 scoped
source files throughout verification. Earlier runs and their shared-source
changes are recorded separately. This is evidence for recorded binaries, not a
frozen release checkout.
`.agents/tmp/modeller-rc/evidence/mdl-playback/milestone.json` retains
the test results, initial failures, validator outcomes, fingerprints and renders.

Quake MDL now has a complete authoring path through the mesh document, inspector,
CLI and package staging. Import retains every indexed skin member, cumulative
skin/pose timing, native frame group and supported header field. Schema 4 keeps
those exact arrays and palette provenance through source save/reopen, history
and recovery; ordinary models continue to use schema 3. Indexed PNG, PCX and LMP
intake preserves duplicate-colour indices and treats index 255 as opaque for MDL.
Native edits share validated candidate operations and cancellation. Selecting a
skin member for preview is session-only and cannot change the saved document.

The MDL writer enforces the original renderer limits, packs half-width UV seams,
preserves native groups/skins, and reports position, UV and normal quantization.
It rejects a triangle that byte quantization collapses or reverses. Seam packing
is deterministic but is not a globally minimal vertex allocation. Palette bytes
remain external to exported MDL files. Original GLQuake's four skin texture slots
and first-interval group timing are reported alongside software Quake's stored
cumulative timing. OBJ omissions are explicit; embedded indexed skins continue
to prevent silent MD2/MD3 conversion.

All 35 modeller suites and four shared level material/package suites pass in the
recorded Windows debug build. The new MDL inspector also passes at an asserted
actual 2× device pixel ratio. Widget renders and semantic controls cover dark and
both high-contrast themes, 100%/200% text, RTL and 50% expanded labels. These checks
caught and corrected inspector overflow; they do not replace physical keyboard
or screen-reader acceptance. Independent synthetic IDPO fixtures cover binary
layout, exact indexed/timing round trips, malformed data, export precision,
native edits, cancellation, undo/recovery, CLI output protection and staging.

`.agents/tmp/modeller-rc/evidence/mdl/milestone.json` records the sanitized results,
renders, validation logs, initial failures, build details and SHA-256 manifests.
All 40 tested binaries remained unchanged. Of 114 scoped source files, the shared
CLI changed during verification; modeller sources remained unchanged. Results
describe those binaries, not a frozen release checkout. An earlier UI test was
mistakenly started before relinking completed, locking the executable and testing
the previous build; that attempt is excluded and retained as diagnostic evidence.
A subsequent sequential build/test and the full final run pass within the normal
test limits. Older test directories remain after the previously recorded cleanup
approval rejection. No new unrelated defect was identified in this milestone.

The later native timing milestone above closes the MDL group/skin playback gap.
Player-colour/fullbright/flag preview and
original-engine acceptance remain open. Ordinary clip preview retains its explicit
session FPS. Automatic level placement remains limited to MD3 for Quake III.
Attached-model assembly, collision
authoring, maximum-scene performance, assistive-technology acceptance and clean
Windows/macOS/Linux release packages also remain required before RC approval.

Smooth clip preview now shares position, normal and rigid attachment interpolation
with generated poses. A monotonic clock samples elapsed time within the selected
range, including end-to-start blending, instead of advancing once per timer
callback. The mesh editor starts with a session-only Smooth preview option;
pausing, stepping or explicitly selecting a frame returns to an exact saved pose.
Reduced motion disables automatic playback. No transient pose enters source,
history, recovery or game exports. Invalid attachment bases or mismatched surface
pose layouts have textual/accessibility diagnostics. Permanent generated poses
still require full validation.

Raster requests retain compact projected attachment markers, so the last completed
image and its tags remain aligned while a later pose or camera request renders.
Content retirement clears the displayed image and markers; smooth playback hides
vertex-edit dots and transform handles until pause. This closes fractional-pose
preview implementation, not child-model assembly, engine timing, collision or
large-model latency acceptance.

Playback fixtures compare transient poses with authored in-betweens across both
cameras, textures, normals, reflected tags and picking. They cover delayed timer
delivery, clip boundaries, source immutability, exact-pose editing, visibility
changes and marker alignment while raster work is pending. Hidden tags skip
projection and interpolation diagnostics; a synthetic native-preview fixture
exercises 8,192 tags per pose across three poses. This observation does not bound
visible-tag or maximum-mesh latency.

The final Windows debug run passes all 30 modeller suites and four shared level
material/package suites within their existing time limits. Playback, animation UI
and viewport checks also pass at an asserted actual 2× device pixel ratio. Widget
renders cover both cameras, dark and both high-contrast themes, 100%/200% text,
RTL and expanded labels. The earlier gesture regression was fixed without
weakening its assertions: an explicit transform gesture pauses playback before
resolving stored-pose handles, while pointer navigation ignores hidden handles.

The smooth-preview evidence fingerprints 97 scoped source files and 35 tested
binaries. During the final build, the shared shell/header and CLI changed; the
playback fixture's tag-count metadata was corrected and explicitly rebuilt.
Between test start and the post-test audit, only the shared Meson file changed
within the source scope, and all 35 binaries remained unchanged.
A later delivery audit records further shared shell/header changes; scoped
modeller sources and all tested binaries still match their test-start hashes.
`.agents/tmp/modeller-rc/evidence/playback/milestone.json` records those limits,
sanitized results, renders, validators and the earlier failures. The evidence
describes the listed binaries, not a frozen release checkout. Concurrent package
build issues and missing package/view-link CLI documentation were corrected;
the earlier Doom translation-extraction issue was resolved by that work. Native
platform, assistive-technology and original-engine acceptance remain open.

Animation authoring now includes indexed clip creation/rename/ranges/deletion,
range preview with session-only FPS, full-pose copying and bounded in-between
generation. Every surface and attachment participates; original poses remain
exact. New normals are normalized and rigid tag orientations use shortest-arc
interpolation with consistent handedness. Invalid generated geometry, cancelling
normals and incompatible tag bases reject the whole edit. Spanning clip ranges
expand, later ranges shift, and endpoint-only clips remain stable. The document
worker, history/recovery, editable source and CLI share these operations.
Native exports identify omitted clip metadata. The smooth-preview follow-up above
adds fractional poses. MDL authoring is covered by the later milestone above;
attached-model assembly, game timing, collision and engine acceptance remain open.

Animation fixtures verify exact endpoint preservation across two surfaces and
rigid/reflected tags, clip boundary shifts, indexed duplicate names, shortest-arc
rotation across 180 degrees, the 1,024-frame boundary, early clip/storage limits,
atomic invalid-pose refusal, cancellation, undo, source/recovery round trips,
MD2/MD3 reload and actual CLI dry runs/writes. The final Windows debug run passes
all 29 modeller suites. Animation, Health and viewport checks also pass with
an asserted actual 2× device pixel ratio. Widget renders cover dark 100% and
both high-contrast themes at 200%, including RTL and 50% expanded editor labels.
The layout checks caught horizontal overflow; inspector sizing now measures
complete pages, including nested group padding, instead of only button labels.

The earlier combined editor smoke exceeded its 90-second limit. Optional timing
markers now distinguish editor work from full-studio construction, and the shell
fixture applies the saved theme before construction as `main.cpp` does. All
existing shell handoff, reentry and deferred-close assertions moved intact into
`model-shell-smoke`; it and `model-editor-ui-smoke` retain 90-second limits. The
final serial run completes them in 32.93 and 18.39 seconds respectively. Timings
vary on this shared host (one CPU sample was 93%); no production latency bound
is inferred from these smoke-test observations.

The run fingerprints 91 scoped source files and 30 binaries. The first audit
after the 29-suite run found no drift. The later evidence audit, after the 2×
checks, records concurrent changes to the shared CLI and Meson files; the other
89 scoped source files and all 30 tested binaries were unchanged.
`.agents/tmp/modeller-rc/evidence/animation-milestone.json` records tests, widget
renders, validators, initial failures and cleanup limits. Its results describe
the fingerprinted binaries, not the subsequently changed shared source files.
A concurrent package-subset compile error was resolved by that work before final
validation. Four disposable directories from the two earlier timed-out editor
runs remain after automatic cleanup approval was blocked. Native keyboard and
screen-reader acceptance, worst-case responsiveness, game acceptance and clean
Windows/macOS/Linux release packages remain open; the wider shared checkout is
not a frozen release source tree.

The Health inspector and `model topology` now share indexed topology diagnostics
for duplicate faces, unused vertices, disconnected fans, winding conflicts,
branching edges and boundaries. Finding selection connects to the component
table and previews. Four explicit whole-surface repairs remove duplicates or
unused samples, split disconnected fans, or orient faces consistently. They
preserve authored UVs/normals and all poses, materials and attachment data,
remap selection/seam marks, and use ordinary history, recovery and CLI output
protection. Orientation retains deterministic component seeds and refuses
branching, duplicate or nonorientable input. It does not infer outside direction.
At this earlier milestone, hole filling and branching-edge cuts were still
open; later boundary tools and nonmanifold splitting add those explicit edits.
Open boundaries remain informational. General geometric self-intersection
checks and repair before strict source admission remain open; the indexed
repairs do not close the broader topology release gate.

Health fixtures verify exact component findings, per-pose attribute preservation,
middle-index compaction, seam/selection remapping, deterministic orientation,
nonorientable refusal, storage bounds, cancellation, history, recovery, source
round trips, animated MD3 export and CLI parity. The recorded Windows debug run
passed 25 of 26 modeller suites within their normal budgets. The editor UI suite
timed out at 90 seconds, then passed alone in 98.19 seconds with a diagnostic
180-second limit; its normal timing gate was unresolved at that milestone (the
animation evidence above records the follow-up). Health and viewport
also passed at actual 2× device scaling. Health renders cover dark 100% and both
high-contrast themes at 200%, including RTL and expanded text. Visual review
caught and fixed horizontal overflow; a regression assertion now checks it.
The dense 131,072-face inspection took 806 ms and the 129,032-face/65,025-vertex
grid took 3,558 ms during this run, with no worst-case latency claim.
Evidence, 92 scoped source fingerprints, 27 binary fingerprints, initial test
corrections and cleanup limitations are recorded in
`.agents/tmp/modeller-rc/evidence/health-milestone.json`. No scoped source drift
occurred during validation; the wider concurrently edited checkout is not frozen.
Native keyboard/screen-reader acceptance, worst-case responsiveness and clean
Windows/macOS/Linux release packages remain open.

Automatic unwrapping and packing now use pinned xatlas through the shared mesh
document worker and CLI. A reviewed build adaptation preserves indexed seams
and avoids independent-axis texel rounding. Selected faces use one square atlas
with explicit pixel padding; unselected UVs and all animation geometry remain
intact. Packing preserves existing island shape, orientation and relative scale.
The wrapper bounds library allocation, supports cancellation and validates
collapsed/overlapping faces before adoption. Atlas fixtures cover curved animated
meshes, coplanar seams, partial selection, deterministic output, cancellation,
memory/storage limits, history, source persistence, MD3 export and CLI parity.
The Windows debug build passed all 24 modeller suites, plus atlas/UV/viewport
checks at actual 2× display scaling. Widget renders cover 100% dark and 200%
high-contrast light/dark, including RTL and expanded labels. The 4,608-triangle
curved fixture completed in 136 ms in this shared development session; this is
not a worst-case latency bound. Source/binary fingerprints, sanitized test logs,
licence-bundle checks and limitations are recorded in
`.agents/tmp/modeller-rc/evidence/atlas-milestone.json` and its `atlas-*` inputs.
The shared checkout is not a frozen release. At this earlier milestone,
rectangular atlases and packing around fixed regions were still open; their
subsequent evidence is recorded above. Persistent per-corner pins, texture
rebaking, broader maximum-mesh throughput and native platform acceptance remain
open.

The first mesh-document implementation is connected through Models > Mesh Editor,
primitive baking, the shared CLI, and package/map handoff. The core smoke test
checks all-frame preservation, tag-aware frame edits, atomic mutation rollback,
UV seam isolation, conforming subdivision, MD3 round trips and export limits,
source conflicts, history, and real CLI commands. The widget test exercises
selection, extrusion/UV/frame edits, undo, shell entry, and 100% dark / 200%
high-contrast light RTL renders with expanded labels. Passing these tests is
an implementation milestone, not completion of the broader gate.

Indexed edge selection now connects the component table, visible-face picking,
3D/UV highlighting, document history, and recovery. Conforming midpoint splits
update every incident face and animation pose. Distance welding uses fixed,
deterministic anchors with UV/normal seam protection and refuses newly invalid
shared-edge topology or pose collapse. The CLI exposes edge incidence through
`model topology` and the same operations through `model edit`. The subsequent
Health workflow adds disconnected-fan detection and four explicit surface repairs;
general hole filling and geometric intersection analysis remain open.
Topology fixtures cover all-frame interpolation and seam checks, deterministic
anchors, collapsed-face removal, duplicate/winding/nonmanifold-edge refusal,
storage limits, comparison-budget exhaustion, cancellation during search, and
real CLI dry runs and writes. Recovery fixtures cover endpoint validation and
older copies without edge data. Renderer fixtures check selected-edge occlusion,
winding, and the distinction between original edges and near-plane cuts.

Precise visible-vertex picking and explicit X-ray selection now share the
component table's indexed selection. A labelled translation gizmo previews
axis or view-plane movement, then commits one validated document operation.
Frame scope, history, and package/material handoff use the existing services.
World-axis delta snapping is shared by the gizmo, numeric transforms, and CLI
`--snap-grid`; exact orthographic presets supplement the perspective camera.
Fixtures cover ray constraints, snapping, all/current-frame edits, cancellation
and late-render retirement, playback pause, camera/frame changes, invalid face
collapse, undo, and CLI dry runs/writes. Widget renders cover 100% dark and
200% high-contrast light with RTL and expanded labels. These checks do not
establish gesture throughput on large meshes, physical
keyboard operation, or native assistive-technology acceptance.

World-axis rotation rings and axis/uniform scale handles now use the shared
transform service. Geometry and the viewport share origin, selection-bounds,
and custom pivots; all-frame edits keep the displayed pose's pivot fixed. Angle
snapping and scale factors relative to 1 match numeric and CLI edits. Rotation
uses ray/plane angles with continuous turns and a projected tangent for edge-on
rings. Preview normals follow the transform before culling; previews remain
separate until one validated commit. Invalid drag positions cancel on release,
and tool/pivot/snap changes retire active gestures. Custom pivot coordinates
appear only when selected, and numeric table cells keep signs on the left in
RTL layouts. The recorded Windows debug build passes all 20 modeller suites;
the manipulation and viewport suites also pass at 2× display scaling. Fixtures
check all three perspective rotation axes, edge-on rings, axis/uniform scaling,
fixed pivots across poses, numeric/CLI parity, invalid-position recovery, context
cancellation, and undo. Widget renders cover 100% dark and 200% high-contrast
light with RTL/expanded labels. Evidence and binary/source fingerprints are in
`.agents/tmp/modeller-rc/evidence/rotation-scale-*`. Concurrent shell, CLI, and build-file
changes are recorded as source drift; this is not a frozen release. Selection/custom
axes are covered by the later transform-axes milestone. Trackball interaction,
large-mesh gesture throughput, and native accessibility
acceptance remain open alongside the broader gate.

Attachment-tag authoring now shares mesh history, recovery and package handoff.
Add/duplicate/rename/delete maintain the same identities in every frame; pose
edits preserve imported orientation and handedness. Tags mode connects named
table selection, origin picking, dashed local-axis overlays, move/rotate previews,
numeric transforms, absolute origins, explicit orientation reset and pose copying.
All/current-frame scope, fixed pivots and move/angle snapping use the existing
document services. CLI inspection and editing use the same validation. MD3
retains all poses, MD2 refuses tags and OBJ frame export reports the omission.
Core and widget fixtures cover atomic failures, names and limits, frame edits,
cancellation, selection-aware undo/recovery, CLI dry runs/writes and MD3 staging.
The smooth-preview milestone above adds interpolated attachment preview.
Child-model assembly, collision authoring and original-engine acceptance remain
open; this does not complete the release gate.
Windows debug evidence now covers all 22 modeller suites. The combined run hit
the editor UI suite's 90-second deadline; an isolated retry passed in 60.4 seconds.
Tag authoring, manipulation and viewport checks also pass at a verified 2×
display pixel ratio. Widget renders cover 100% dark, both 200% high-contrast
themes, RTL and expanded labels. Standard pane scrollbars remain necessary for
wide tables and expanded forms. Evidence, binary/source fingerprints, the timeout,
and concurrent shared-source drift are recorded in
`.agents/tmp/modeller-rc/evidence/tags-*`; this is not a frozen release build or
physical keyboard/screen-reader acceptance.

UV authoring now includes indexed-edge seam marks, island expansion/picking,
face detachment, origin/selection/custom pivots, shared offset snapping, and
dragged movement with one undo step. The UV view preserves texture aspect,
repeats materials, and offers pan, cursor-centred zoom, and selection framing.
Topology analysis and clipped drawing use a separate cancellable worker with
a four-million-pixel limit. Source schema 3 and recovery retain seam marks and
MD2 skin dimensions; schemas 1 and 2 remain readable. Native exports retain resolved UVs and split indices,
while marks remain authoring metadata. Separate per-island transform pivots
remain open. Core/CLI and widget fixtures cover source
validation, marked boundaries, all-pose preservation, topology remapping,
pivots, snapping, cancellation, stale work retirement, shared selection, and
single-step undo. These checks do not establish maximum-mesh interaction
latency, native keyboard/screen-reader acceptance, or cross-platform release
readiness.
The Windows debug fixture completed a 32,768-triangle UV chart/render request
in 650 ms at 1× and 827 ms at 2× display scaling while servicing GUI timer events.
These are observations from one shared development session, not release latency
bounds. Widget renders cover 100% dark and 200% text in high-contrast dark with
RTL/expanded labels, at both display scales. The separate 3D/material fixtures
also cover the high-contrast light theme. An older designer handoff assertion
now waits for the asynchronous level-preview worker before checking the same
staged prop geometry.

Animated MD2 export now shares mesh validation, GUI/CLI diagnostics, source
history/recovery, and package staging. Source schema 3 retains the skin size;
older schemas default to 256 by 256. Exact all-pose position/normal duplicates
can share XYZ indices without losing UV corners, and ordered skin slots retain
duplicates. The writer enforces original-renderer limits, emits matching software
triangles and GL commands, reports measured position/normal/UV loss, and refuses
quantization collapse or reversal in any pose. The shared MD2 decoder checks that
GL strips/fans agree with indexed triangles and UVs; mismatches, malformed streams,
and invalid normal indices cannot silently become an editable source through file
import or package-browser adoption.

The mesh editor prepares package exports on its cancellable document worker.
MD2 package paths disable automatic level placement, whose supported route remains
MD3 into a Quake III map. Tests cover raw MD2 fields and commands, compact strips
and fans, malformed-stream refusal, all-frame round trips, schema compatibility,
ordered skin slots, undo/recovery, PAK output, and actual CLI dry runs/writes.
All 20 modeller suites and four package integration suites pass in the final
recorded Windows debug run. A 2,048-position, 512-pose fixture (1,048,576 frame
vertices) exported 4,426,160 bytes in 1.60 seconds;
this is one local observation, not a latency guarantee. Widget fixtures exercise
skin settings, undo, asynchronous handoff, and 100% dark / 200% high-contrast light
RTL renders with expanded labels. Evidence is retained under
`.agents/tmp/modeller-rc/evidence/md2-*` in the shared working checkout.
The integration run also verifies retained imported-image previews and preserves
original input-path protection when a package plan becomes a dependency subset.
Binary fingerprints and source drift are recorded with the evidence; concurrent
changes to the checkout do not constitute a frozen release build.
Original-engine playback, PCX size/content verification during handoff, optimized
GL-strip output, source-port profiles, differing GL-only data, and game-specific
MD2 entity/animation integration remain unverified or open. MDL writing has since
been implemented; original-engine acceptance and the broader release gates above
remain open.

The earlier primitive-designer tests cover deterministic static MD3/OBJ output,
schema 1/2 compatibility, all-axis transforms, part UV transforms, selection-aware
undo, package/map handoff, and widget rendering at 100% and 200% RTL. They do not
establish general mesh authoring or a professional release candidate.

Mesh recovery now uses bounded, checksummed local copies and an atomic background
writer with one active and one replaceable pending snapshot. The editor checks
dirty documents every five seconds, retires copies on save/discard/clean undo,
and restores verified copies as unsaved drafts. The chooser and CLI share header
scanning and full payload verification. Recovery tests cover cancellation,
corruption, source preservation, coalescing, late-write retirement, and draft
restoration. Recovery alone does not complete the interruption, external-change,
or large-document responsiveness gates.

The model file service now checks destination identity again after staging and
publishes new outputs without replacement. Its fixtures inject cancellation,
concurrent destination creation, external edits, and competing writer locks.
Late cancellation after commit remains a successful save. Source-load candidates
remain separate until accepted, and topology expansion checks frame-storage
limits before allocating added poses. The document worker now covers preparation,
import, edits, source saves, and export. Cancellation checkpoints cover geometry,
validation, editable JSON conversion, and output writing; native decoder and Qt
codec calls check cancellation at their boundaries. Worker tests cover UI event
delivery, cancellation, failed-candidate preservation, undo adoption, and late
cancellation after a save. Editor tests cover locked controls, rejected reentrant
edits, and deferred close. These checks do not establish worst-case cancellation
latency, view-refresh responsiveness, package handoff performance, or native
screen-reader operation.
Closing the studio during mesh work now cancels and waits for that operation,
resolves unsaved mesh changes, and resumes the studio close once the editor
accepts it. Cancelling the unsaved prompt clears the pending continuation, so
an independent later editor close cannot close the studio unexpectedly. This
does not establish equivalent deferred-close handling for other busy services.

Authored surface materials now share the level editor's bounded image/shader
resolver and immutable staging snapshots. Package replacement, deletion, undo,
reload, and palette changes invalidate the material binding; geometry-only edits
reuse it. One background loader and one replaceable request prevent stale results
from reaching the 3D or UV views. Cancel, Reload Images, textual counts, and Details
expose the state. `model materials` supplies the same diagnostics through the CLI.
The fixtures exercise snapshot isolation, missing/ambiguous paths, shader image
precedence, staged palettes, read/image budgets, coalescing, cancellation, disposal,
and rendered 3D/UV replacement at 100% and 200% RTL/expanded text. Unassigned
surfaces remain unassigned after a source round trip instead of inheriting another
surface's material from aggregate metadata. This does not establish
full shader rendering, external `.skin` overrides, or a large-package latency bound.

The software viewport now uses per-pixel depth, perspective-correct texture
sampling, retained UVs at the near plane, and depth-sorted translucent fragments.
Renderer tests cover intersections, submission order, transparency, matching
picking, shared-edge coverage, malformed geometry, cancellation, and bounded render allocation;
widget integration checks both camera projections, worker completion, request
coalescing and stale-material retirement. Throughput is measured with a
2,048-triangle textured scene at 1,024 square. This does not establish large-map
or cross-platform performance. Native browser loading now uses the cancellable
worker described above. MDC, MDR, and IQM
geometry remains unsupported. The foundation release notes in
`RELEASE_CANDIDATE.md` describe an older product snapshot and must not be used as
evidence for the modeller gate above.
