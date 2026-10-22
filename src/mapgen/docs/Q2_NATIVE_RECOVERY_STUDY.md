# Ravel: recovering after a drop

Five of eight fixed native profiles finish with a low-speed, grounded pause in
the declared landing area. Releasing input succeeds at both approach inputs.
An eight-command turn toward the south then release also succeeds at both;
the corresponding northward turn succeeds at forward200 and fails at400.
Continuing forward fails the recovery goal at both inputs. All three failures
remain in the results (`out/native-recovery/comparison.json`, written by `python -B -m tools.native_movement.recovery`).

This extends the earlier Ravel drop probes (`out/ravel/native-routes/README.md`, upstream study evidence, not imported),
which established a standing grounded arrival while retaining horizontal
momentum. The [new evidence record](../knowledge/q2/native-recovery-study.json)
binds the fixed protocol, complete native frames, compiled pocket checks and
six analysis controls. No Ravel geometry or historical movement record changes.

## Fixed conditions and success definition

The MAP/BSP remain the same frozen Ravel inputs; the BSP SHA-256 is
`8b88bf771c64783a812aa20160250da60201d625ba8eb8d7920b006e7097b69c`.
Every profile starts at nominal feet `(176,16,256)`, zero velocity and yaw180°,
with an explicit0.125-unit floor separation. Eight zero-input16ms commands
establish the standing grounded start and are excluded from travel times.
Gravity remains800; side, up and pitch remain zero throughout.

The initial approach is fixed separately for literal forward400 and200. It
lasts46 /49 movement commands, ending at the previously observed first middle
floor landing: `(-30.375,16,128.125)` / `(28.625,16,128.125)`. Every saved native
frame through that boundary matches the earlier respective drop trace exactly.
The new phase starts on a scheduled command, without observing the player or
adjusting inputs during execution. The schedule uses prior results; it is not
a blind prediction of human reaction time.

The protocol (`out/native-recovery/protocol.json`, written by `python -B -m tools.native_movement.recovery`) was written before execution
and rejects changed inputs or goals on rerun. Four recovery profiles per speed
have an identical256-command budget:

- **Continue:** hold the original forward input and yaw180°.
- **Release:** set movement input to zero after the fixed approach.
- **North8-release:** command yaw90° and the same forward input for8 commands,
  then zero movement for248 commands.
- **South8-release:** the same burst at yaw−90°, followed by release.

A90° change in commanded yaw is an abrupt synthetic input. It is not evidence
that the player followed a perfect right-angle path or completed a corner.

The recovery goal uses player **center** X−128…80 and Y−56…56, feet128±1,
standing32×32×56, dry, grounded, horizontal velocity magnitude at most10 and
absolute vertical velocity at most10. Nine consecutive qualifying frame-end
samples must span128ms: eight16ms intervals. Eight samples alone would span
only112ms. Continuous stability between samples is not established.

After the first middle-floor contact, leaving the larger center envelope
X−144…96 / Y−64…64 or losing the required floor state permanently fails the
attempt. A later wall stop or return cannot erase that violation. The finite
input set, tolerances and envelopes were not widened to make failures pass.

## Geometry that makes the pocket possible

The full middle-floor bounding box is not treated as walkable. The west slab
has X−240…−80 and Y−240…80; its eastern neighbor has X−80…320 and Y−80…80.
Their coplanar union provides the tested pocket. Five touching horizontal
32-unit hull strips tile the entire recovery envelope's occupied footprint;
four connecting legs are also swept against all163 blocking world brushes.

The compiled proof (`out/native-recovery/pocket-support.json`, written by `python -B -m tools.native_movement.recovery`) covers the
occupied prism X−160…112, Y−80…80, feet128 to head184. It partitions actual
horizontal convex top faces for continuous support and checks the full hull
against stored world planes. Boundary touching is permitted by this geometric
test; it is not a claim that every touching pose or every trajectory moves
freely under native collision rounding. Native samples separately check their
actual standing hulls and0.125-unit floor separation.

## Outcomes

Times below include the initial approach and completed128ms sampled dwell,
excluding the initial settling commands. All successful final velocities are
zero. The number in parentheses is time from first middle-floor landing to
completed dwell.

| Fixed profile | Forward400 | Forward200 |
| --- | --- | --- |
| Continue | Fail | Fail |
| Release | 1,200ms (464ms) | 1,184ms (400ms) |
| North8-release | Fail | 1,312ms (528ms) |
| South8-release | 1,328ms (592ms) | 1,312ms (528ms) |

Release ends at center `(-66.5,16)` / `(5.125,16)`. Relative to their first
landings, those traces use36.125 /23.5 additional westward units before the
accepted pause. These are measured fixed-input instances, not universal stopping
distances or optimized braking. No reverse input was used.

The successful south bursts pause at `(-70.25,-40.125)` / `(0.375,-19.25)`.
The north200 burst pauses at `(0.375,50.25)`. North400 reaches Y63.875 and
eventually rests at `(-68.625,63.375)`, outside the predeclared goal's Y≤56.
It remains inside the larger transient envelope and on the middle floor. Its
failure is **insufficient clearance inside the required pause area**, not a
recorded fall. Neither goal nor envelope is a universal map-design threshold.

The north passage ends at X−64. A separate static standing pose at
`(-68.625,72,128)` overlaps compiled jamb/wall brushes62 and64; the same pose
at X−96 is clear. These two checks demonstrate a full-hull alignment constraint
beyond the recovery area. They do not attribute a particular native collision
callback or prove an executed alternative route. North200 stops before the
doorway; its success does not establish navigation through that opening.

Both continuation traces eventually repeat the same XY position while native
stored X velocity remains−300 /−200. Their final centers are X−139.625 /
−141.125, Y16. The stationary position does not satisfy the required residual
speed or goal position. That distinction is preserved in the data, without
assuming a specific collision/quantization cause.

The shared runner also retains its broad X≤0 station diagnostic in each
`result.json`; study success lives separately in `recovery.json`. The two
continuation profiles pass the station diagnostic but fail recovery. Conversely,
the forward200 release and turn profiles recover east of X0 and never satisfy
that station diagnostic. Neither result replaces the other.

## What this adds to the design rules

**Reserve space for the action after arrival.** The previous drop goal ended
with momentum. The new release traces use additional space and time before a
qualified pause. A declared landing point alone is insufficient to describe a
recovery pocket.

**Inspect the intended turn with a full player hull.** Opposite90° input bursts
consume different available space here. A point near a doorway can still leave
part of the player against its jamb. Recovering inside a pocket and navigating
through the next opening are separate tests.

**Use position, velocity and contact together.** A repeated position can coexist
with nonzero native velocity, while a valid recovery can occur before an older
horizontal station. Define the outcome needed by the room rather than accepting
whichever diagnostic first reports success.

These results support design hypotheses for staging areas and exit alignment.
They do not calibrate universal stopping margins, prove faster escape, compare
combat advantages or train model weights. The next useful trials should add
an explicitly bounded onward corner route, controlled timing variation around
the landing and opposing traffic.

## Validation and limits

The [orchestrator](../tools/native_movement/recovery.py) loads the unchanged
runner into a separate output namespace. It calls the private pinned official
Quake II3.19 CM/Pmove baseline against world model0, retaining complete commands,
native frames, stderr, station diagnostics and recovery outcomes. No engine
client, input device, renderer, game module, dynamic entity, damage logic,
weapon, camera, sound, opponent or network is involved. Releasing or turning is
synthetic usercmd data consumed by the standalone harness, not injected user
input. External implementation and executables remain outside these artifacts.

Six controls check vector speed and vertical velocity, an out-of-pocket stop,
the128ms dwell interval, reset after a speed spike, rejection after envelope
overshoot, and rejection after loss of the required floor. All pass. The final
verifier independently derives each recovery event from the raw native state,
rechecks the fixed approach against the earlier traces and repeats the compiled
pocket proof. Three failed profiles remain bound alongside five successes.

```powershell
python -B -m tools.native_movement.recovery
python -B -m unittest discover -s tests -p test_native_recovery.py -v
python -B -m tools.native_movement.recovery --verify
python -B -m tools.native_movement.recovery --record
```

No unrelated implementation defect was established. No scratch directory,
external asset copy or cleanup attempt was needed.
