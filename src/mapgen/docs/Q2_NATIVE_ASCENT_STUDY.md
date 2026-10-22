# Talus Bench: native ramp and stair movement

The matched Bench ramp and eight-tread variant now have actual native movement
evidence. All **24 primary journeys and eight deterministic repeats** complete
with a dry, standing, grounded goal. Uphill completion times tie in all three
lanes at both fixed input values. Stair descent reaches a grounded goal later;
the retained planar crossing samples show that landing recovery explains most
of that difference. These results do not rank multiplayer quality.

The [experiment record](../knowledge/q2/native-ascent-study.json) binds the
complete results (`out/native-ascent/comparison.json`, written by `python -B -m tools.native_movement.ascent`), command streams, native
frames, source identity proof and five analysis controls. This extends the
[earlier static comparison](Q2_RAMP_STAIR_COMPARISON.md) with a separate pinned
Quake II 3.19 world-collision experiment. The historical comparison remains an
unchanged record of what had been established at that time.

## Held geometry and protocol

The baseline BSP remains
`6ac8fb109af4062fc08b51c5bc766edb36fd728f5afc7d2461f127c821329b03`;
the stair BSP remains
`b41c235f024f351c903c4da813512478ed50f75b285b9e75ff411f06d7daf740`.
The source and compiled audits are executed again before and after the trials.
They independently retain 173 unchanged world brushes and 81 unchanged nonworld
entities; one wedge becomes eight expected boxes. Complete source face strings,
compiled planes, full texinfo and contents are compared. Worldspawn changes only
its display title. Materials, items, light entities and all other geometry match.
Neither published map is regenerated, recompiled or modified.

Bench's incline occupies X−736…−576 and Y−160…320. Both versions span 160 units
in width, 480 in horizontal run and 128 between their flat floors. The ramp rises
from an 8-unit entrance lip at a 1:4 slope; the control has eight 60-unit treads
with 16-unit rises. These internal profiles are the intended intervention.

Each journey starts stationary at Y−208 / feet 0 for ascent, or Y368 / feet 128
for descent: 48 units outside each end and 576 units between station planes.
Three lane centers, X−712, −656 and −600, exercise the center and both sides.
The standing hull is 32×32×56; the side lanes leave eight units to the authored
incline edges. The actual launch feet are explicitly 0.125 units above the
specified floor, then eight zero-input 16 ms commands establish a stable
standing grounded start. Those 128 ms are excluded from measured travel.

The single command phase holds forward 400 or 200, yaw 90° uphill or −90°
downhill, level pitch, zero side/up input, gravity 800 and 16 ms commands. It
does not jump, steer or adapt to the geometry. Each trial retains its entire
400-command movement budget, including frames after its first accepted goal.
Launch order is counterbalanced; each center journey is repeated once in a
fresh process, with identical complete command and native frame bytes. These
are deterministic consistency checks, not independent human observations.

Completion requires crossing the destination station plane, remaining within
one unit of the lane center throughout the approach, reaching the destination
floor within one unit, zero waterlevel, a clear standing hull and native ground
contact. A further bound rejects grounding more than 32 units beyond the station.
The maximum accepted overshoot is 29.5 units on forward-400 stair descent.
Crossing the horizontal plane while still falling is recorded separately.

## Observed result

The following values are identical across the three lanes, resolved to one
16 ms native command. They are simulated command time, not wall-clock time.

| Direction / literal forward input | Ramp grounded goal | Stairs grounded goal | Stairs minus ramp |
| --- | ---: | ---: | ---: |
| Up / 400 | 1,984 ms | 1,984 ms | 0 ms |
| Up / 200 | 2,976 ms | 2,976 ms | 0 ms |
| Down / 400 | 1,968 ms | 2,064 ms | +96 ms |
| Down / 200 | 2,928 ms | 2,976 ms | +48 ms |

The lower station explains why an endpoint definition matters. With forward
400, both geometries cross it at 1,968 ms. The ramp is already grounded at feet
0.125; the stair trajectory is airborne at feet 11.5 and grounds 96 ms later.
With forward 200, the ramp crosses grounded at 2,928 ms; the stairs cross at
2,944 ms with feet 4.25 and ground 32 ms later. Thus the +48 ms grounded result
contains a 16 ms planar crossing difference and 32 ms of subsequent recovery.
Calling these simply “stair speed penalties” would discard the actual evidence.

During ascent, the stairs move the feet upward in exactly eight 16-unit updates
while native stored Z velocity stays zero. The ramp distributes ascent over
101 / 152 frame updates for forward 400 / 200, including the entrance lip; its
largest upward update is 9.125 / 8.375 units. Both remain grounded through their
uphill journeys and rise exactly 128 units. The unchanged native step routine
can preserve the non-step Z velocity after selecting a step movement, so a zero
Z-velocity series is not proof that feet height stayed constant. The native
maximum wishspeed is 300, consistent with the retained forward-400 goal velocity
of 300; the labels are literal inputs, not speeds in units per second.
[Official movement implementation](https://github.com/id-Software/Quake-2/blob/master/qcommon/pmove.c).

Both downhill profiles also have frames whose native grounded flag is false.
The ramp has 96 / 143 such command-end samples, and the stairs 82 / 96, for
forward 400 / 200. These counts do not establish smoother contact or safer
movement. They classify sampled native state rather than continuous subframe
flight, impacts, client camera motion or fall damage. All journeys have zero
waterlevel, remain standing through the goal and contain no embedded hulls.

## Design rules supported by this pair

1. **Separate crossing from recovery.** Record the first horizontal station
   crossing and the first usable landing. A route that reaches an exit in the
   air has not necessarily restored the same movement options as a grounded
   arrival. Here the distinction changes the interpretation of the descent.
2. **Do not rank ascents by step count alone.** The graph adds seven configured
   step penalties when one ramp becomes eight treads, while both fixed uphill
   native times tie. The graph remains a connectivity/cost policy; it has not
   become a calibrated timer. This one fixture does not determine a replacement
   universal step weight.
3. **Measure position and state together.** Feet displacement, native velocity
   and ground contact answer different questions. A step's discrete position
   change can coexist with zero stored vertical velocity. Camera and aim
   effects require a renderer and actual player/weapon observations.
4. **Preserve useful flat landing space.** These specific no-jump downhill
   commands recover at different distances after the lower station. A future
   room should test the landing against barriers, perpendicular traffic and
   exits, rather than copying the 29.5-unit observed overshoot into a universal
   clearance rule.

The new Ravel layout can use a ramp and stairs for different approach roles,
without asserting that one is inherently faster or better. Broad turns, local
visibility and the placement of recovery landings remain authored hypotheses
to test in that geometry. This study does not establish Ravel traversal.

## Scope and reproduction

The [optional native harness](../tools/native_movement/README.md) calls the
unmodified official Quake II 3.19 CM/Pmove functions against world model 0.
There is no game module, client, renderer, input device, sound, item pickup,
damage, oxygen, dynamic player/entity collision, network or opponent. The
private GPL-linked executable and external source pins are verified, not copied
into the MAP/BSP package or repository source. This is not a claim of movement
equivalence to Q2REPRO or another Quake release.

The [new orchestrator](../tools/native_movement/ascent.py) loads the unchanged
runner into an isolated module namespace whose output root is
`out/native-ascent`. It retains the runner's preparation, BSP bounds preflight,
native build verification, initial settling, goal and failure handling. The
historical runner and `out/native-movement` artifacts are left untouched.

```powershell
python -B -m tools.native_movement.ascent
python -B -m unittest discover -s tests -p test_native_ascent.py -v
```

Five controls protect the newly added interpretation: airborne planar crossing
versus grounded completion, excluding a post-goal fall, feet displacement
versus stored velocity, rejecting a distant recovery or missing frame, and
isolating the output root. All pass. The 32 complete native attempts are the
integration evidence; no new gameclient launch or input permission was needed.

Matched human turning, jumping, passing, pursuit, visibility while moving,
camera/aim response, sound and combat remain untested. The full original
ten-paired-human-journey regimen is still pending; deterministic repeats do not
fulfil it. No unrelated code defect was found in the shared native runner.
