# Weft physical gates and fixed turning policies

All sixteen new first attempts completed a physically bounded stair–gallery–stair
circuit and the unchanged low-speed finishing pocket. Both rolling turns and
24-frame corner releases passed eight of eight. Pauses reduced final lateral
offsets but added time. No Weft geometry was changed and no trial was adjusted or
repeated after its result.

This follows the [original eight failed circuit attempts](Q2_NATIVE_MOTIF_CIRCUITS.md).
Their results remain **0/8**, unchanged. A separately declared physical-contract
interpretation of those same raw frames passes **2/8** without executing native
movement again. The **16/16** new result uses newly derived timing and the new
physical contract; comparing it directly with the historical0/8 would confound
timing and scoring. The timing-only comparison is between the eight matched
rolling/release pairs within this new experiment.

## Frozen geometry and physical contract

The two routes remain Crossing6's rotation0 crosswalk and Gallery1's rotation1
horseshoe, both with128-unit platforms and two eight-tread stairs. Starts, ends,
32×32×56 standing hull, gravity800,16ms commands, eight stationary settling
frames and the final pockets are unchanged. The exact polylines and all commands
are in the frozen protocol (`out/native-motif-turning/protocol.json`, written by `python -B -m tools.native_movement.motif_turning`).

The actual128-unit stair width permits player centers ±48 from its middle while
the entire32-unit hull fits laterally. That band is enforced throughout each
stair phase, not only at a checkpoint. Axial progress from the first riser lies
in[-48,316]: the lower wall's full-hull inset through the upper station304 plus
its12-unit axial tolerance. Sampled airborne descent between treads is allowed;
the contract does not require a whole footprint to lie on one tread height
through every riser transition.

Six ordered gates remain: ascent midpoint, upper entry, first corner, second
corner, upper exit, descent midpoint. The upper entry/exit use ±48 across the
stair and ±12 along it, with grounded feet128±1. Between accepted entry and exit,
every native frame must remain dry, standing and grounded at128±1. Every upper
footprint and consecutive upper chord must have full compiled floor support and
continuous hull clearance. Supported endpoints alone cannot admit a gap.

The geometry checker uses the frozen all-SOLID AABB world and exact floor-union
coverage over conservative bounding rectangles for each upper chord. Those
chords project native XY to feet128. They certify that reconstructed horizontal
geometry, not the unrecorded native internal substeps or arbitrary hull heights
within128±1. Native frame-end ground/contact and post-movement clear-hull checks
are separate. The four original stair configuration paths and complete nominal
upper paths remain bound through the earlier58-file geometric evidence record.

The finish still requires the original clipped pocket: Crossing Y[544,576] or
Gallery X[-72,-40], with ±32 across the nominal endpoint. After final release,
nine consecutive frame ends must span128ms, grounded and dry at feet0±1, with
horizontal velocity norm≤10 and |vertical velocity|≤10. Earlier route failures
are permanent. The ±48 physical band therefore does not silently widen the
finishing pocket.

## Protocol-only interpretation of the historical traces

No new native process participates in this control. It changes the declared
interpretation while preserving and hashing all historical inputs and outputs.
It is a separate physical-contract control, not a claim that only one tolerance
changed: along-axis gates narrow, lateral gates match actual stair width, and
complete upper-support checks replace the former centerline corridor rule.

| Historical profiles | Physical-contract result |
| --- | --- |
| Crossing forward200; Gallery reverse200 | Pass complete route and unchanged finishing dwell |
| Crossing forward400; Gallery reverse400 | Route accepted; lateral final position misses unchanged pocket |
| Crossing reverse200/400; Gallery forward200/400 | Full upper-footprint support is lost at an edge |

The original strict failures remain available beside this new2/8 interpretation.
This resolves the earlier ambiguity between a narrow exit-checkpoint miss,
actual partial support and finishing-position error.

## Timing derived before the sixteen new runs

The proposal, physical contract and exact command arrays were frozen before
evaluating the historical control or executing the new factorial. Timing is
informed by earlier native evidence; this is not a blind first study of Weft.
The original official Quake II3.19 source is the separately installed,
hash-pinned test dependency. Reading its friction, acceleration and eighth-grid
position/velocity behavior informed the estimates; no engine source was copied
into this implementation.

The first ten stationary-start native frames cover19.375 units at input200 and
30.5 at input400 in the sampled away-from-zero direction. Steady increments are
3.125 and4.75 per frame. Toward-zero timing estimates add0.125 per moving frame;
all selected legs retain one coordinate sign. This is an explicit transfer
assumption informed by the native signed-coordinate representation, not a
replacement movement simulator or a universal quantization formula.

Earlier grounded Ravel releases stopped after18/22 commands, with measured
coasts23.5/36.125 for input200/400 in their respective signed-coordinate cases.
The declared transferred coast estimates are21.375/23.5 for input200 away/toward
zero and36.125/38.75 for input400. The source's friction6 and stopspeed100 plus
those observed stopping durations motivate a24-frame release. Transfer from
Ravel to Weft is a hypothesis; the new traces independently show whether the
corners stop.

For the first two legs, choose the nearest integer command count whose measured
startup plus steady increments and estimated subsequent coast reach the corner.
Exact half-count ties choose the earlier command. The final moving phase targets
the center of the first tread before releasing input for128 frames. This keeps
descent recovery as a native test instead of pretending a flat-floor coast
predicts airborne stair descent.

| Motif / direction | Input | Moving command counts |
| --- | ---: | --- |
| Crossing forward | 200 | 128 / 192 / 111 |
| Crossing forward | 400 | 82 / 124 / 75 |
| Crossing reverse | 200 | 128 / 183 / 111 |
| Crossing reverse | 400 | 82 / 120 / 75 |
| Gallery forward | 200 | 199 / 203 / 180 |
| Gallery forward | 400 | 129 / 133 / 121 |
| Gallery reverse | 200 | 199 / 212 / 180 |
| Gallery reverse | 400 | 129 / 137 / 121 |

Each row has two policies with identical moving commands, yaw changes and
acceptance: immediate rolling turn, or24 zero-input commands before each yaw
change. The latter adds48 commands/768ms. Input400 is an analog command amplitude;
the native wish-speed limit is300, not a400-unit/s speed claim. All sidemove,
upmove and pitch are zero. There are no feedback corrections, position resets,
candidate searches or corner certificates substituted for complete trajectories.

## Matched results

All sixteen completed routes pass their complete ordered gates, physical stair
bands, upper support/clearance and finishing dwell. Every declared24-frame pause
ends with exact zero native velocity. Full commands and9,268 raw native frames
are retained. Times exclude the eight initial settling frames and include the
completed final128ms dwell.

| Motif / direction | Input | Rolling dwell completed | Paused dwell completed | Final lateral offset rolling / paused |
| --- | ---: | ---: | ---: | ---: |
| Crossing forward | 200 | 7,520ms | 8,208ms | +8.125 / +1.500 |
| Crossing forward | 400 | 4,928ms | 5,680ms | +13.750 / +0.125 |
| Crossing reverse | 200 | 7,376ms | 8,064ms | −14.125 / +1.625 |
| Crossing reverse | 400 | 4,864ms | 5,616ms | −21.500 / +1.250 |
| Gallery forward | 200 | 9,840ms | 10,560ms | +10.875 / −0.625 |
| Gallery forward | 400 | 6,544ms | 7,296ms | +9.250 / −1.875 |
| Gallery reverse | 200 | 9,984ms | 10,704ms | −6.625 / 0.000 |
| Gallery reverse | 400 | 6,608ms | 7,360ms | −4.125 / +2.125 |

Pauses reduce absolute final lateral offset in every pair, from4.125–21.5 units
to0–2.125. They add688–752ms to the accepted dwell time under these scripts.
That differs from the768ms of extra command time because changed coast/restart
states also change the later descent and recovery. This does not identify a
universal best turning policy or a gameplay advantage.

Every accepted final state has zero velocity and feet0.125. Crossing finishes at
Y544.125 and Gallery at X−40.125: after accounting for the hull, each is0.125 from
its room wall. These are wall-adjacent recovery results. They do not demonstrate
equivalent stopping precision in open space, nor identify individual collision
callbacks as the cause of a stop.

Five paused trials fail the shared runner's broad one-axis station diagnostic:
Crossing reverse200/400, Gallery forward200/400 and Gallery reverse400 stop just
short of the nominal centerline crossing. They still satisfy the declared
bounded pocket and complete physical circuit. These diagnostic failures remain
in each original `result.json` and in the comparison; they are not replaced by
the independent `physical-circuit.json` outcome.

## What this teaches and what remains open

The measured128-unit crosswalk and horseshoe circuits can be traversed with
fixed inputs and recovered within the declared pocket without enlarging the
geometry. Earlier timing corrections suffice for both tested policies. A full
corner stop improves alignment here, but rolling turns also pass. Test route
geometry, checkpoint meaning, lateral carry-over and final stopping separately
before treating every failed script as an architectural defect.

Other rotations,192-unit platforms, narrow48-unit horseshoe legs, different
coordinate placements, starting lanes, speeds, human steering and populated
matches remain outside this experiment. An open-space finish or a bounded
corner-lane perturbation study would test robustness beyond these exact starts.
No game module, dynamic entity, pickup, fall damage, client, renderer, input
device, opponent, human performance or learned model weights were involved.

## Evidence and verification

| Binding | SHA-256 |
| --- | --- |
| Frozen Weft BSP | `980b22bf6aaedb032df1f04db5cc46d864d72f961a9852809486fbd64abf50f0` |
| Frozen canonical layout | `aaddb7862052e2c3b0264ef99952f356f78dffd45ef23eb70e31eb3e7de6adf7` |
| New predeclared protocol | `07ee02088ccc15b147fd014735c4c16d3fb5640eb99c705879cdc80d293e1110` |
| Original circuit record | `cc6af5ee0eb7bd72b4144ebb339708f12210cd9a0b628f6fa44b61c4f89cd855` |
| New comparison | `4645a98a57b140307e2c5d6ca883705484b84dea66f88775b6aed743efe8f731` |

`python -B -m tools.native_movement.motif_turning verify` independently
re-derives the eight protocol controls and sixteen new native results without
launching the executable. The retained
paired inspector (`out/native-motif-turning/inspect_pairs.py`, upstream helper script, not imported) derives all
paired timing, offset, pause-state and wall-gap observations from frozen frames.
`python -B -m out.native-motif-turning.verify_evidence` also checks the complete
file manifest and independently re-derives those paired observations.

Eleven controls pass: physical48/49-unit boundaries, between-gate stair
containment, airborne upper samples, exit-boundary chords, missing ordered
gates, eight-versus-nine state dwell, velocity-norm interruptions, final pocket
overhang, missing frames and supported-endpoint/unsupported-middle counterexamples.
No scratch was created or cleaned, no native dependency changed, and all earlier
evidence files remain hash-identical. No unrelated issue was found.
