# Native Weft motif circuit study

Eight predeclared first attempts failed the complete circuit protocol. All eight
nevertheless climbed the first stair, reached both upper corners, contacted
intermediate treads on the second stair and eventually reached the lower floor.
Four maintained the full standing hull inside the actual stair width; their
failure includes a checkpoint narrower than that width. The other four used
partial footprint support near an outer edge. These outcomes separate a strict
route certificate from native passage through the geometry. They do not establish
that either motif is untraversable.

The original commands and failed classifications are retained unchanged in
the evidence directory (`out/native-motif-circuits/`, written by `python -B -m tools.native_movement.motif_circuits`). No command was adapted,
rerun, or adjusted after examining its movement. The later
observations (`out/native-motif-circuits/observations.json`, upstream study evidence, not imported) describe actual
support and final states without promoting any failure to success.

## Frozen inputs and scope

The experiment uses the final generated Weft artifact, independently of later
generator changes. The original
[official Quake II 3.19 movement and collision source](https://github.com/id-Software/Quake-2)
runs through the existing pinned private native harness. Its source, executable,
build record and shared runner were unchanged. No dependency source or binary
was copied into the study or map package.

| Input | SHA-256 |
| --- | --- |
| Weft BSP | `980b22bf6aaedb032df1f04db5cc46d864d72f961a9852809486fbd64abf50f0` |
| Weft MAP | `a00415003b3a72aaa206ad38d8ea02f5a12296441b70d746539bda41b31aa3c6` |
| Canonical layout | `aaddb7862052e2c3b0264ef99952f356f78dffd45ef23eb70e31eb3e7de6adf7` |
| Protocol | `3423e5bd2380326e1fb61f26614bcd188c7a128b3d42a8d88eb17bed604c116b` |
| Native executable | `ce4e7d365a7949ec4d2d252b9eda534f1373574255b4e1c6e25fa2090e328aac` |

Only compiled world model zero participates. There is no game module, client,
renderer, connected player, keyboard/mouse input, damage, pickup, opponent or
dynamic-entity test. These are deterministic scripted movement experiments, not
human performance, optimal navigation, balance or trained model weights.

## Fixed route and timing contract

Crossing 6 is a rotation-0 raised crosswalk. Its forward feet centerline is
`(88,552,0) → (88,960,128) → (696,960,128) → (696,552,0)`.
Gallery 1 is a rotation-1 horseshoe with the centerline
`(-48,-1168,0) → (-680,-1168,128) → (-680,-496,128) → (-48,-496,0)`.
Reverse trials start at the opposite lower station and reverse these points.
Each route ascends one complete eight-tread stair, negotiates two right-angle
turns, crosses the entire upper route and descends the other stair.

Every start is stationary with an explicit 0.125-unit feet clearance, a standing
32×32×56 hull and eight zero-input settling frames. Gravity is 800 and commands
are 16 ms. Settling is excluded from reported movement times. Pitch, sidemove and
upmove are zero. Three uninterrupted forward-input phases change yaw at their
fixed boundaries, followed by 128 zero-input frames. There are no corner pauses,
position resets or observation-driven adjustments.

The timing hypothesis uses `min(forwardmove,300)` units/s: literal input 400 is
subject to the native 300-unit/s wish-speed limit, not a 400-unit/s speed claim.
For each planar leg the frame count is
`ceil((distance + 24 - final_leg*32)/(nominal_speed*0.016))`.
The 24-unit acceleration allowance and 32-unit final coast reserve are coarse
geometry-based assumptions fixed before execution, not fitted native travel
measurements. They are part of what this first experiment tests.

| Motif | Input | Three moving phases, frames | Final release | Total native frames |
| --- | ---: | --- | ---: | ---: |
| Crossing 6 | 200 | 135 / 198 / 125 | 128 | 594 |
| Crossing 6 | 400 | 90 / 132 / 84 | 128 | 442 |
| Gallery 1 | 200 | 205 / 218 / 195 | 128 | 754 |
| Gallery 1 | 400 | 137 / 145 / 130 | 128 | 548 |

There are six ordered gates: ascent midpoint, upper entry, first corner, second
corner, upper exit and descent midpoint. Midpoints constrain height and travel
axis while allowing the actual stair's ±48-unit full-hull cross lane; they do not
require grounding during descent. Upper entry/exit use ±24 XY and grounded feet
128±1; the corners use ±48 XY. The dry standing hull must remain within the
declared centerline corridor of half-width 48 and feet Z[-1,129]. Grounding at
height128 is required from accepted upper entry through accepted upper exit.
Leaving an invariant permanently fails the circuit, even if it returns later.

After every gate, the finish requires nine consecutive frame ends spanning
128 ms, grounded and dry at feet0±1, horizontal velocity norm≤10 and |vertical
velocity|≤10. Final pockets are centered within32 units of the nominal finish
and clipped to the actual standing-hull clear floor: Crossing Y[544,576],
Gallery X[-72,-40]. Their cross lanes are ±32. The wall-to-first-riser gap is64,
so the 32-wide hull leaves a32-unit deep clear center strip. This geometric
stopping-space descriptor is not a universal minimum dimension.

## Independent compiled geometry checks

The audit binds all selected source solids to exact compiled AABB vertices:
32 treads, two crosswalk/gallery systems, four stair runs. It checks a continuous
piecewise standing-hull configuration path across every16-unit rise and36-unit
run, and full-footprint support at every tread center. The vertical pieces
represent geometric clearance across risers, not simulated jumps or a claim of
continuous support at one height while stepping.

Both entire planar upper routes, all four96×96 occupied-hull turn pockets and
all four occupied lower goal pockets have continuous clearance and exact floor
union coverage. The static audit examines166 all-SOLID model-zero brushes and
18,260 brush/sweep pairs. All are separated by exact AABB bounds or boundary
contact; no additional SAT sweep is needed. The original centerline is therefore
geometrically certified before considering how the fixed native inputs depart
from it.

## Retained results and physical observations

All eight first failures reach the first four gates, then miss `upper_exit`.
For Crossing forward and Gallery reverse the first recorded violation occurs
when ordinary descent becomes airborne while the unmatched exit gate keeps the
upper-floor invariant active. This diagnostic does not mean the player fell off
the gallery. Crossing reverse and Gallery forward first exceed the declared
±48 route corridor during turn carry-over.

The exit offsets below are measured at the first native axial crossing of the
actual upper-exit plane, independently of acceptance. Signs refer to the cross
axis. “Full stair band” asks whether every sampled center on the descent run
keeps the whole32-unit footprint inside the128-unit-wide stair cross section.

| Motif / direction | Input | Exit offset | Whole descent in full stair band | Later low-speed goal-pocket dwell |
| --- | ---: | ---: | --- | --- |
| Crossing forward | 200 | +26.875 | yes | 7,712 ms |
| Crossing forward | 400 | +43.000 | yes | absent |
| Crossing reverse | 200 | −55.625 | no; partial edge support | absent |
| Crossing reverse | 400 | −62.750 | no; partial edge support | absent |
| Gallery forward | 200 | +59.625 | no; partial edge support | absent |
| Gallery forward | 400 | +67.750 | no; partial edge support | absent |
| Gallery reverse | 200 | −25.375 | yes | 10,240 ms |
| Gallery reverse | 400 | −42.125 | yes | absent |

The two later dwells are descriptive only: their earlier circuit requirements
failed. Their final feet are `(722.875,544.125,0.125)` and
`(-40.125,-1193.375,0.125)`, both with zero velocity. All eight full-budget final
states are grounded at feet0.125 with zero velocity. Only those two finish inside
the declared goal pocket. No result is silently widened or rescored.

Every sampled footprint during the longitudinal second-stair run overlaps its
physical cross section. All eight register intermediate grounded contacts:
input200 contacts tops128/96/64/32; input400 contacts128/80/32. Airborne frames
between those contacts are expected observations of this descent, not proof of
an off-platform fall. No complete sideways separation from the stair footprint
is observed. The four edge routes nevertheless lose full footprint support;
native `grounded` is not a certificate that the whole32×32 base is supported.

For Crossing forward and Gallery reverse, every reconstructed horizontal
upper-plane chord through the actual exit plane is continuously clear and its
conservative occupied rectangle has full floor support:258/177 chords for
Crossing input200/400,416/283 for Gallery. The opposite directions have partial
support near the outer edge:239/258,160/177,328/416 and221/283 fully supported
chords respectively. All those chords remain clear of solid volume.
These are exact geometric certificates for reconstructed chords between native
samples, not recordings of native internal substeps. Stair transitions are kept
as native sampled contacts and separate piecewise geometric clearance checks.

The shared runner's broad one-axis station passes all eight. That diagnostic
requires neither the full route history nor its low-speed finish. It is retained
to show why a simple destination crossing overstates this experiment.

## What changes next

First validate gate placement and tolerance against the actual certified route
width, while preserving this strict first record. Then compare a finite declared
set of corner timing/braking policies on unchanged geometry, measuring lateral
carry-over, full footprint support and final recovery separately. Direction
reversals exhibit different offsets under identical phase lengths; the current
record does not isolate timing, absolute-coordinate quantization and other
causes. It does not justify blindly widening the architecture or claiming one
direction is inherently better.

Rotation0 crosswalk and rotation1 horseshoe at128 units are the only generated
motifs covered here. The other rotations,192-unit variants,48-unit horseshoe
legs, different player profiles, populated matches and human steering remain
untested by these circuits.

## Verification and retained files

`python -B -m tools.native_movement.motif_circuits --verify` independently
re-derives all eight original outcomes from their full raw frames and checks
the fixed protocol and native inputs. It does not launch the native executable.
`python -B -m out.native-motif-circuits.verify_evidence` also verifies the complete
manifest and independently re-derives the post-execution observations.

Ten Python controls check missed-gate false positives, residual velocity,
eight-versus-nine sample dwell, interruption/reset behavior, permanent route and
floor failures, timestamp order, union support across a seam and partial edge
support. All pass. The eight movement experiments themselves remain failed.
Every trial retains its JSON command specification, expanded command text,
complete raw frame stream, native stderr, broad station result and independent
circuit result. No scratch was created, no previous output was edited and no
cleanup was attempted. No unrelated issue was found.
