# Model Engine Acceptance

## Quake III Native Animation Configuration

The optional `src/tests/model_q3_animation_engine_oracle.py` check compiles
unmodified `CG_ParseAnimationFile`, `CG_RunLerpFrame` and `COM_Parse` functions
from a supplied GPL-2.0-or-later Quake III source checkout. Small file and
client-state adapters replace engine services. It compares the exported header,
all 31 adjusted animation records and 33 playback-step selections per record
against values sampled by the studio. Fixtures include reverse, once-only clips,
loop tails and rates from 0.001 to 1,000 FPS. Build outputs preserve the upstream
header, source digests, native output and comparison record.

Run `model-q3-animation-smoke` from the configured Meson build with
`VIBESTUDIO_MODELLER_EVIDENCE` pointing to an existing project test output
directory to generate `animation.cfg` and `animation-expected.json`. Then run:

```sh
python src/tests/model_q3_animation_engine_oracle.py \
  --source /path/to/Quake-III-Arena \
  --fixture .agents/tmp/modeller-rc/native-fixture/animation.cfg \
  --expected .agents/tmp/modeller-rc/native-fixture/animation-expected.json \
  --output .agents/tmp/modeller-rc/native-oracle
```

The output directory must be new and under the project's `.agents/tmp` tree.
Meson/Ninja and a C++ compiler are required; no engine executable, game assets,
display, input injection or screenshots are involved. This verifies native
parsing and steady playback-step selection. It does not verify game-state
transition blending, catch-up clock policy, haste, procedural aiming, player
bundle layout or original-engine gameplay. See the
[authoring and export contract](MODEL_ASSEMBLY.md#quake-iii-native-animation).

Native exports need an independent engine check: importing a writer's output
through the matching decoder can hide a shared mistake. The optional
`src/tests/model_engine_workflow.py` workflow creates original geometry, textures,
palettes, QuakeC and maps, builds through the real studio CLI and external map
compilers, and starts an FTE dedicated server against independent fixture copies.

## Face Orientation

Editable meshes and OBJ use counter-clockwise front faces:
`cross(b - a, c - a)` points outward. MDL, MD2 and MD3 store clockwise front
faces. Native import reverses the second and third corners after format checks;
native export performs the inverse conversion. Stored vertex normals, UVs,
seams, positions, tags and animation poses retain their identities. Both MD2
triangle and GL-command streams receive the same conversion. Numeric edits,
normal rebuilding, OBJ handoff, package staging and level model loading share
this convention.

Earlier unreleased builds copied native corner order directly into the editable
mesh and wrote editor order directly to native files. Newly exported primitives
and OBJ meshes could consequently face inward in a game. New imports and exports
correct that boundary. **Existing editable files keep their recorded triangles**;
the loader does not guess orientation from normals or file names. Re-import an
unchanged original native model when possible. For previously edited native
imports, preserve a backup and review face orientation before exporting; use
Flip Faces and Recalculate Normals only on the intended surfaces. Old exports
that already contain reversed faces also need review. This is not an automatic
migration of every experimental source.

`model-winding-smoke` independently reads native triangle/position records,
checks the MD2 GL stream, and verifies two-pose import, normal rebuilding and
OBJ handoff. It does not use the production decoder to decide whether the
writer's native winding is correct.

## Reproduction

Supply a built VibeStudio CLI, VibeMap2 `vibemap2-bsp` (or ericw-tools
`qbsp`), VibeMap3 (or q3map2), FTE's server-only executable and FTEQCC.
No tool is downloaded or installed by the workflow.
These optional GPL tools stay separate executables; no engine library is linked
into VibeStudio. Use an unmodified reference source/build and retain its revision
or source inventory beside the evidence.

```sh
python src/tests/model_engine_workflow.py --binary <vibestudio> --qbsp <vibemap2-bsp> --q3map2 <vibemap3> --engine <fteqw-sv> --qcc <fteqcc> --output-root .agents/tmp/modeller-engine-proof
```

On Windows, supply local paths to Linux ELF server/compiler binaries and append
`--wsl-distribution Ubuntu` (or the configured distribution name). The workflow
uses WSL's `/mnt/<drive>` mapping; studio/map compiler invocations remain native.
The native Windows FTE console is deliberately unsupported by this harness.
On Linux, `env` and GNU `timeout` must be available. Other POSIX environments
need those compatible commands; this workflow does not itself prove a macOS
release build.

The output root must be a new directory below this repository's `.agents/tmp`.
Input/output ancestors cannot be links or reparse points. Inputs are copied,
never hard-linked. Every process has an explicit working directory, bounded
runtime and closed standard input. The server is dedicated, uses `-nostdin`,
`-nohome`, isolated configuration and disabled listening/publication settings.
The server workflow uses no client, fullscreen window, mouse/keyboard automation
or screen capture. The separate offscreen rendering workflow below uses the
engine's registered screenshot command. Tests that control physical input or
launch fullscreen still require explicit permission for those actions.

The workflow first repeats the collision compiler proof, including local box
scaling and world rotation/translation. Each BSP29/IBSP38/IBSP46 fixture then
checks the same exported models and collision through FTE:

- MDL/MD2/MD3 frame counts and engine frame lookup, with native mesh traces
  hitting both authored poses on the front and passing through the back.
- A grouped MDL's native frame count and duration.
- MD3 tag identity, per-pose origins and orientation observations.
- Empty point traces through clip volumes, blocked player/large hulls, an
  overlapping start, an unobstructed path, room floor contact, and `walkmove`
  success/refusal without keyboard input. A player-style `SOLID_SLIDEBOX`
  entity selects FTE's player-clip mask for sized traces and movement.

Two additional server runs deliberately lower only the second MD2 pose and
remove the clip volumes. These must fail the matching assertions. They prevent
a vacuous pass caused by missing model loading or inactive collision checks.
The runner records commands, diagnostics, source/asset/executable hashes, an
independent MD3 layout audit, assertion names and intentional failures.
The minimal QuakeC fixture uses FTE's named-field lookup. The reviewed FTEQCC
reports Q208 because these system definitions differ from the complete original
games; the diagnostic is retained with the run. This fixture targets FTE.

## Findings And Limits

The reviewed FTE source snapshot synthesizes MD3 frame names (`frame0`,
`frame1`, etc.). The runner checks that engine lookup behavior and independently
verifies the authored names in the exported MD3 records.

The same snapshot's `Mod_GetTag` treats the stored MD3 basis as matrix rows,
while `gettaginfo` returns matrix columns. A +90-degree Z tag reports its
forward axis as -Y. Original Quake III preserves the stored basis vectors;
VibeStudio's exported bytes contain the expected +Y first basis. The test keeps
the rotated-axis assertion: it is recorded as an **engine finding**, not a pass,
and is not worked around by transposing the exported file. See the linked
[source references](CREDITS.md#model-native-engine-acceptance).

Exit 0 means the positive assertions passed and both negative controls produced
exactly their intended failures. Exit 4 means the workflow completed with the
specifically recorded rotated-tag finding;
`verified.json` says `completed-with-engine-findings`. Unexpected failures abort
without a final verification record. A future FTE build with correct tag behavior
can pass without that finding. Negative-control failures are listed separately.

This is FTE server acceptance of generated data, including Quake II/III BSPs
loaded under the fixture's QuakeC game. It does not establish Quake II game-DLL
or Quake III game-VM behavior, native client movement, original-engine acceptance,
packaging or platform UI readiness.

## Offscreen Native Rendering

`src/tests/model_render_workflow.py` adds an independent visual check with a
supplied, unmodified Linux FTE client, FTEQCC and Pillow. FTE must include its
`egl_headless` renderer; the environment must provide Mesa EGL and llvmpipe.
These are optional test tools, not studio runtime dependencies. The recorded
client build uses the same inventoried reference source as the server, with
`FTE_USE_SDL=OFF`, `FTE_ENGINE=ON`, `FTE_ENGINE_SERVER_ONLY=OFF` and
`FTE_ENGINE_CLIENT_ONLY=OFF`. No renderer implementation is imported.

```sh
python src/tests/model_render_workflow.py --binary <vibestudio> --engine <fteqw> --qcc <fteqcc> --output-root .agents/tmp/modeller-render-proof
```

Windows again requires `--wsl-distribution <name>` and local paths to the Linux
engine/compiler. The same fresh-output-directory, link rejection, independent
copy, bounded process and source-hash rules apply. The runner disconnects X11
and Wayland display selection, forces software rendering, closes stdin, disables
mouse/joystick/audio and keeps configurations/caches under the output root.
FTE draws into an EGL pbuffer; it opens no desktop window. `vid_fullscreen 0`
and `-window` are explicit. Only the engine's `screenshot` command writes the
PNG captures from its render target; no operating-system capture or input
automation is used.

The real studio CLI creates an original indexed quadrant texture and a two-pose
quad, then exports MDL, MD2, MD3 and a grouped MDL. An original MenuQC scene
renders the three formats side by side under fixed ambient lighting. It records
both poses, their midpoint blend and two discrete grouped-MDL samples. An
independent perspective calculation checks projected position and size, visible
face coverage and four asymmetric texture colours in every panel. The runner
also requires native frame counts/names and the grouped-frame duration reported
by the engine. Each case produces five 640×480 engine screenshots, 15 panel
checks and 11 engine identity/timing checks.

Three additional cases deliberately reverse MD2 faces, flip MD3 texture V, and
connect the first MDL skin texel to a used colour region. Exactly the affected
format's five panel checks must fail; all other panels and engine identity
checks must pass. These controls catch back-face culling, UV orientation and
native skin preprocessing. `verified.json` is written only after the positive
case and all three controls meet their expectations and input hashes remain
unchanged. `cases.json` retains pixel observations even on an unexpected visual
failure.

### Engine Behaviors Relevant To Authors

FTE normally tries replacement model extensions, including MD3, when loading
MDL/MD2. A same-stem export can therefore hide the intended native file. The
fixture uses distinct stems, disables `r_replacemodels` before loading, and
checks frame identities. The initial same-stem rendering probe is not evidence
that all three native readers were exercised.

GLQuake-style MDL loading flood-fills the colour connected to the first skin
texel as background to reduce mipmap halos. A used UV region of that same colour
can visibly change. The positive fixture uses an original palette with black at
index 0 and a single black top-left guard texel; it leaves the engine's flood-fill
behavior enabled. Its negative control removes that guard and verifies the
visible change. This is native renderer behavior, not an export-index remap.
The studio preserves the authored indices and does not emulate this preprocessing
in its previews. Keep background padding separate from used UV regions and
validate the target engine; see [Quake MDL](MODEL_MESH.md#quake-mdl) and the
[source credits](CREDITS.md#model-native-engine-acceptance).

This fixture proves a small, generated scene through one FTE/Mesa configuration.
It does not establish full model/shader lighting, player-colour/fullbright effects,
animated skin timing, attachment rendering, original Quake/II/III gameplay,
physical input or Windows/macOS/Linux release packages. The
[modeller release gates](MODELLER_RELEASE.md) remain open.

## Baked Assembly Animation

`src/tests/model_assembly_render_workflow.py` extends offscreen acceptance to
an original three-part chain authored entirely through the studio CLI. It uses
the same supplied FTE/FTEQCC/Pillow prerequisites and the shared isolation in
`model_render_common.py`:

```sh
python src/tests/model_assembly_render_workflow.py --binary <vibestudio> --engine <fteqw> --qcc <fteqcc> --output-root .agents/tmp/modeller-assembly-render-proof
```

On Windows, add `--wsl-distribution <name>`. All output, independent asset copies,
configurations and caches stay inside that fresh repository directory. The
engine uses its registered `screenshot` command against the EGL render target.

The root loops, the child uses a slower rate and fractional phase before
clamping, and the grandchild holds stored poses while looping. Animated tag
origins and rotations compose through both attachment levels, alongside local
translations, rotations and accumulated scales. Tilted textured panels provide
nontrivial normals and perspective. The CLI samples five poses at 2 FPS starting
at 0.5 seconds, exports a multi-surface MD3, and explicitly joins compatible
baked surfaces before MD2 export. The original recipe and components must remain
byte-for-byte unchanged. Source schema 6, clip timing, dependency fingerprints
and export reports are checked.

The assembly uses schema 3 skin links: loose files for the root/grandchild and
an exact package occurrence for the child. Source materials deliberately name
a nonexistent texture. Only the linked skins assign the generated quadrant
texture, so a lost override cannot pass the positive image checks. Skin input
digests, occurrence identities and before/after assignments are checked, and
all original model/skin inputs remain byte-for-byte unchanged. Direct MD2 skin
and MD3 shader records must contain the resolved material path.

A closed-form fixture oracle computes expected vertices and normals without
calling the studio sampler or decoder. Direct native-byte checks verify every
pose against format quantization, frame names and surface/vertex counts. FTE
then renders both formats, five stored poses and two midpoint blends of the
baked vertices. Each screenshot has six independently checked panels: projected
bounds, convex silhouettes and four asymmetric texture samples. Silhouette
checks use pixel centres, a one-pixel border allowance and at least 99% interior
coverage; bounds allow two pixels. Brightness includes neutral colours at
filtered texture boundaries. The positive fixture achieves full interior
coverage with no pixels outside that border.

Three fresh engine runs deliberately replace one MD2 pose with another, offset
the MD3 grandchild, and flip the MD3 child's texture V. Exactly the predicted
9, 7 and 7 panel checks must fail, with the expected geometry or texture reasons;
all unaffected panels and engine frame identities must still pass. Each of the
four cases produces seven 640×480 screenshots, 42 panel checks and six engine
identity checks. Hashes, commands, observations and native-byte errors are
retained; `verified.json` is published only after every case passes its expected
outcome and fixture/tool/source hashes remain unchanged.

This proves composed geometry and linked-skin material handoff for a small generated baked
clip in one FTE/Mesa configuration. The engine blends baked vertices; it does
not reconstruct the original curved tag motion between samples. Configure
native game animation timing separately. Live engine attachment semantics,
game-specific animation configuration, full shader/lighting behavior,
animated collision, maximum-size assemblies, original Quake II/III gameplay
and platform release packages remain outside this evidence.

## Native Player Package Loader

`src/tests/model_player_bundle_engine_oracle.py` accepts a user-supplied original
Quake III source checkout, an optimized VibeStudio CLI and the original
`PlayerBundleFixture` retained by the core suite when `VIBESTUDIO_MODELLER_EVIDENCE`
is set. It exports default/custom-skin PK3s through the CLI, checks archive hashes
and native MD3 byte layouts independently, and compiles original player discovery,
skin/token parsing, animation parsing and TGA decoding functions with small
filesystem/renderer shims. Missing model, skin, configuration and icon cases must
fail. Pixel checks cover the icon and both quadrant textures in full.

```powershell
python src/tests/model_player_bundle_engine_oracle.py `
  --source E:/_SOURCE/_CODE/Quake-III-Arena-master `
  --binary .agents/tmp/modeller-rc/build-release/src/vibestudio.exe `
  --fixture .agents/tmp/modeller-rc/evidence/player-bundle/widgets-integrated-1x/native-fixture `
  --output .agents/tmp/modeller-rc/evidence/player-bundle/oracle-example
```

The output must be a fresh directory under this project's `.agents/tmp` tree.
Source fragments preserve GPL notices; receipts retain source, fixture, application
and harness hashes. No commercial assets, OS input, game window or screen capture
is involved. Model registration and shader lookup are shims: this proves layout,
lookup, parsing and synthetic image pixels, not gameplay, live rendering, shader
semantics or tag interpolation. See [credits](CREDITS.md#quake-iii-animation-configuration).
