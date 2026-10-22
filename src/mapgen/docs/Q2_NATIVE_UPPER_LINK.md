# Hinge: a complete upper inter-room rotation

The frozen Hinge map supports the tested upper passage and both stair approaches. Seven of eight first room-to-room rotation profiles passed the predeclared ordered-route and stopping contract; all four straight upper transfers passed. The remaining fast rolling rotation missed a turn checkpoint while still supported. It remains a failed profile, even though the retained trace subsequently descends the intended stair and settles in the destination pocket.

These are bounded Quake II 3.19 world-collision and Pmove observations. They do not establish multiplayer balance, pickup contact, fall damage, or a complete ground-return circuit.

## Frozen geometry and intervention controls

The final normal-pipeline Hinge layout (`out/hinge/layout.json`, written by the `mapgen_mp_portfolio_export` program) keeps candidate seed `8709371129873691315`. Its added connection joins Crossing 4 and Crossing 6 at feet 128. The 160-unit-wide portal occupies `[344,600,128]..[392,760,320]`. Explicit 24-unit slabs cover the portal and the two branches to existing decks 37 and 73. The early Gallery 3→Crossing 4 proposal is not the selected connection.

The static transfer runs from `(264,680,128)` to `(616,680,128)`. Its 128-unit standing-center band expands to the full 160-unit floor width. The compiled audit (`out/native-upper-link/geometry-audit.json`, written by `python -B -m tools.native_movement.upper_link`) checks exact unions of coplanar top faces and complete standing-hull clearance, including center and both edge lanes at Y616, Y680 and Y744. This avoids inferring support from the center or four isolated corner points.

| Compiled world | Full upper strip | Three geometric lanes |
| --- | --- | --- |
| Normal Hinge | Fully supported and clear | All supported and clear |
| Portal closed by one blocking brush | Supported, blocked | All blocked |
| Explicit portal slab removed | Clear, 1,536 square units of missing feet-128 support | Y616 and center supported; Y744 unsupported |

The missing region is the portal's 48-unit length overlapping 32 units of the lower passage. The center lane remains supported by the compiled shell. A center-only test would therefore miss this defect. All 57 point entities remain byte-identical in the authored entity tail and equal in the compiled entity records. The controls alter exactly the declared world brush, preserving their separate experimental status.

Both selected stair runs retain eight 16-unit rises, 36-unit tread depth and 128-unit width: Crossing 4 east stairs 38–45 and Crossing 6 north stairs 82–89. The audit covers every tread's full standing-footprint center rectangle, the ground entry, and full-width static raise-18/advance/lower translations. These translations include the hull above the previous tread before advancing. They are geometric clearance constructions, not recordings of native internal stair substeps.

The separate lower-route audit (`out/hinge-portal-controls/lower-route-audit.json`, upstream study evidence, not imported) retains full-width support and 56-unit clearance through the lower portal in all three compiled worlds. The upper missing-floor control does not silently become a blocked lower-door comparison.

## Predeclared movement protocol

The plan (`out/native-upper-link/plan.json`, written by `python -B -m tools.native_movement.upper_link`) and protocol (`out/native-upper-link/protocol.json`, written by `python -B -m tools.native_movement.upper_link`) were written and hashed after all three static controls passed and before any native attempt. The complete forward rotation is:

`(216,1080,0) → (216,680,128) → (680,680,128) → (680,984,128) → (1080,984,0)`

The reverse uses these exact points in reverse order. Each has 1,568 units of nominal XY travel across four legs: ascend the first room's stair, turn across its deck and the upper link, turn within the second deck, then descend its stair to ground. There are separate ordered ascent, turn, transfer, descent and ground-finish gates. A single recorded state cannot advance multiple gates; a failed route condition cannot be repaired by later arrival.

Stair center lanes use the actual full-hull width of ±48, with feet in the declared −1..129 envelope and ordinary sampled airborne descent permitted. Upper phases require dry, grounded standing states at feet 128±1. Every consecutive upper frame pair receives an exact reconstructed horizontal hull-chord check and conservative full rectangular support-union check. All declared flat-phase rectangles, including the turns and final ground pockets, were separately certified in the compiled world before execution.

The two factors are literal forward input 200 or 400 and immediate rolling turns or a 24-frame input release before each new yaw. Both pause variants retain identical active commands. Input 400 is capped to the native 300-unit wish speed; it is not a 400-unit movement-speed claim. No side input, jump, button, pitch change or adaptive steering is used.

| Direction / input | Four active rotation counts | Straight transfer count |
| --- | --- | ---: |
| Forward 200 | 119, 145, 94, 116 | 110 |
| Forward 400 | 78, 94, 60, 77 | 70 |
| Reverse 200 | 119, 90, 139, 116 | 105 |
| Reverse 400 | 78, 58, 91, 77 | 68 |

All commands use 16ms steps, eight initial settling frames and a final 128-frame release. Rotation pause variants add 24 release frames at each of the three corners. The straight controls use `(264,680,128) ↔ (616,680,128)` with no intermediate turn.

Counts derive from the pinned earlier Weft stationary-start traces and Ravel release traces: a ten-frame startup prefix, steady eighth-grid increments and an estimated coast, distinguished by travel toward or away from coordinate zero. All timed axes stay on one side of zero. The final descent follows the earlier first-tread-center timing policy without reserving coast. This is an explicit transferred timing approximation, not a new fit to Hinge trajectories or an optimal movement policy.

An accepted finish requires nine consecutive dry, clear, standing, grounded frame-end states spanning 128ms after final release begins, with horizontal speed norm ≤10 and absolute vertical speed ≤10. Rotation goals use the actual supported ground pockets: Crossing 6 X1064..1096/Y936..1032, or Crossing 4 X168..264/Y1064..1096. Straight goals retain ±16 XY precision around the far station and a 32-unit longitudinal corridor overhang. Those are protocol margins within larger supported spaces.

## Twelve first attempts

Times below are movement time through completion of the qualifying dwell, excluding the initial 128ms settle. The entire commanded budget remains in the raw files, including frames after first acceptance.

| Rotation | Input | Corner release | Ordered route + dwell | Completion |
| --- | ---: | ---: | --- | ---: |
| Forward | 200 | 0 | Pass | 8,048ms |
| Forward | 200 | 24 | Pass | 9,248ms |
| Forward | 400 | 0 | **Fail: turn phase/gate deviation** | — |
| Forward | 400 | 24 | Pass | 6,512ms |
| Reverse | 200 | 0 | Pass | 8,208ms |
| Reverse | 200 | 24 | Pass | 9,072ms |
| Reverse | 400 | 0 | Pass | 5,328ms |
| Reverse | 400 | 24 | Pass | 6,432ms |

| Straight upper transfer | Input | Route + dwell | Completion | Accepted final X |
| --- | ---: | --- | ---: | ---: |
| Forward | 200 | Pass | 2,160ms | 617.25 |
| Forward | 400 | Pass | 1,584ms | 615.625 |
| Reverse | 200 | Pass | 2,080ms | 263.125 |
| Reverse | 400 | Pass | 1,552ms | 262.75 |

The fast forward rolling failure first occurs at frame 256, movement time 3,968ms: feet `(747.75,971.75,128.125)`, grounded and dry. Its reconstructed feet-128 chord is completely supported and clear. Y is 0.25 short of the turn gate's lower bound 972 while X is already 3.75 beyond the declared turn rectangle's maximum 744. The following sample reaches Y972.5 but X752.5 remains outside that gate. The adjoining top tread supplies physical support beyond the declared deck rectangle.

The retained tail stays within the actual departure stair/ground center band X664..1096/Y936..1032 and the −1..129 vertical envelope, reaches ground at 5,184ms, and completes a later nine-state goal-pocket dwell at 5,328ms. These observations explain a supported protocol deviation. They do not replace the failed ordered-route result, certify a newly widened gate, or establish an off-platform fall.

For the matched input-400 forward pair, the final yaw change starts after a sampled state at `(687.875,936.625,128.125)` with velocity `(0,300,0)` for rolling, versus `(681.625,982.125,128.125)` with zero velocity after the pause. The changed carry-over state is measured. Its benefit here does not establish that pausing is a good human multiplayer tactic.

The runner's broad coordinate diagnostic is deliberately separate. It passes the failed rolling rotation. It fails the accepted reverse-200 paused rotation because that path settles at X218.25 instead of crossing X216, and fails the accepted forward-400 straight transfer at X615.625 instead of crossing X616. The independently declared two-dimensional pocket and ordered route determine this study's acceptance.

Six of the seven accepted rotations finish 0.125–0.5 units from the destination room's outer wall. Reverse-200 rolling finishes 16.875 units from that wall; its nearest vertically overlapping static box is a stair 15.125 units away. The raw `accepted_wall_distance` field measures the nearest static box, so it must not be read as an identified wall or a causal collision diagnosis. These stopping results do not certify braking in open space.

## Evidence, validation and limits

The comparison (`out/native-upper-link/comparison.json`, written by `python -B -m tools.native_movement.upper_link`) retains all 12 trial identities, command profiles, outcome labels and native diagnostic differences. Each trial folder contains the exact specification, expanded commands, all raw frames, stderr, native result and independently derived physical-route record. The summary (`out/native-upper-link/summary.json`, upstream study evidence, not imported) and its read-only diagnostic source (`out/native-upper-link/observations.py`, upstream helper script, not imported) preserve the supported failure and later pocket observation without rescore. All 5,375 raw frame-end states remain available.

The independent verifier rederives all 12 native summaries and ordered route outcomes, exact command arrays, stable starts, source/compiled geometry checks, row labels, and BSP/runner/build/executable/file hashes. The 20 focused synthetic controls pass; they include a center-pass/edge-fail floor hole, support union seams, raised-hull stair obstruction, ordered-gate failures, nine-state dwell, velocity norm and altered factorial timing/direction. Logs are retained in out/native-upper-link (`out/native-upper-link`, written by `python -B -m tools.native_movement.upper_link`).

| Bound evidence | SHA-256 |
| --- | --- |
| Final Hinge BSP | `3277367e74cab28b858c6bd2c4fcf07effa0c96a8eb1e4d469c10647399caddb` |
| Canonical layout | `a739204e3e0b3d5f9ff543bd06627be1d6eba0ae6a42e78650f114a4c4a9ec92` |
| Protocol | `2365c50595c6a29badb5010e5591345b587090862e7a229f1f6c7a8f09186c7b` |
| Comparison | `62eb891751c0f744dde90271d6e5076c5bc53ef2c9eeb4b83add0a495f3848ce` |
| Diagnostic summary | `a42e240ffcae91892868bd5bb6a5f31a1d8c0afba8fbaaa9497fb2d318fb1c2b` |

No native runner, harness source, executable, earlier experiment or map was changed during this study. No client, game module, user input, rendering, OS capture or new scratch directory was involved. Static whole-footprint constructions and reconstructed frame chords do not expose native internal substeps. No pickup contact, fall damage, combat, spawn selection, multiplayer quality or complete ground-return loop is claimed.

The concrete next design question is how to bind ordered turn gates to the connected deck-and-top-tread space while preserving explicit route order, then test turn carry-over with a new predeclared experiment. This retained failure does not justify widening the architecture or retroactively changing the current protocol. No unrelated issue was found beyond the documented distinction between the broad station diagnostic and the study's ordered physical contract.
