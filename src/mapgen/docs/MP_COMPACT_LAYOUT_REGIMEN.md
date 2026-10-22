# Compact multiplayer spaces: study and test regimen

Study how routes occupy the same architectural volume before making the map
smaller. A gallery, underpass, offset atrium and horseshoe can bring useful
destinations together while preserving different approaches. Exterior gaps may
also separate sightlines or define a courtyard; eliminating every gap is not a
quality objective.

The [spatial pattern study](MP_SPATIAL_PATTERNS.md) records reference observations
and target-engine contracts. The Quake Live atlas (`out/ql-atlas/index.html`, written by `python -B -m tools.ql_atlas.package`)
provides local orthographic views from six actual pre-Steam BSP revisions. The
[Fold experiment](Q2_FOLD_STUDY.md) applies a selection of these ideas to original
Quake II construction. [Lockyard](Q2_LOCKYARD_STUDY.md) follows with an outdoor
court and deeper culvert, informed by the [courtyard/liquid study](Q2_COURTYARD_LIQUID_STUDY.md).
The [interleaved-route study](Q2_INTERLEAVED_ROUTE_STUDY.md) and original
[Ravel layout](Q2_RAVEL_STUDY.md) next examine nested routes, real elbow clearance
and selective vertical choices in a smaller envelope.
[Weft](Q2_WEFT_STUDY.md) follows with compact crosswalk/gallery/barrier motifs
selected by the actual default Q2 deathmatch/duel path across a fixed portfolio.
Keep reference geometry, design hypotheses, original construction, bounded
native movement and future player observations as distinct evidence sets.

The retained atlas contains **64 PNG orthographics** across Campgrounds, Blood
Run, Aerowalk, Furious Heights, Toxicity and Lost World: fitted overviews,
common-scale overviews and nonempty 128-unit height slabs. These are analytical
projections of the pinned local pre-Steam BSPs, not engine screenshots or
certified floors. The local package (`out/ql-atlas/ql-orthographic-atlas.zip`, written by `python -B -m tools.ql_atlas.package`)
contains the diagrams and provenance, without raw BSPs or game textures.

Patrick Williams's [multiplayer design guide](https://robotrenegade.com/articles/multiplayer-level-design-guide.html)
offers useful expert context for distributed resources and several approaches to
important spaces. It also identifies the risk of teleporters concealing weak
physical connectivity. Treat those principles as hypotheses with exceptions,
not numerical acceptance rules. The [Level Design Book's circulation method](https://book.leveldesignbook.com/process/layout/flow)
helps organize route tracing; actual trips and encounters still need observation.

## Read each reference in three passes

1. **Whole-map packing.** Compare the six maps at a common world-unit scale.
   Identify shared walls, short room-to-room transitions, overlapping projected
   routes, deliberate courts and long peripheral detours. Then fit each map to
   view for local detail. Do not mistake equal display size for equal map size.
2. **Local encounters.** Trace a resource approach, alternative approach and
   withdrawal. Inspect whether upper routes cross a lower encounter, follow its
   edge, or sit behind a screen. Mark where a player must turn, expose a flank,
   give up height, enter liquid or commit to a teleporter.
3. **Height and ambiguity.** Inspect each height band independently. A projected
   crossing may have no connection, insufficient headroom or a one-way drop.
   Surface triangles, item entities and target links do not prove player
   navigation. Keep mode flags and the exact BSP revision beside each observation.

For generated examples, compare floor plans with the same XY origin, envelope
and map-unit scale. The current viewer keeps these fixed while switching floors
and retains other-floor outlines as dashed context. An independently fitted
floor can look equally large despite covering a much smaller part of the map;
neither a dashed outline nor shared XY establishes a walkable connection.

Quake Live's movement, weapons, resource rules and map revisions are not Quake II
defaults. Transfer a spatial mechanism, then rebuild and validate it with the Q2
hull and native behavior. Keep the local pre-Steam atlas outside the Q2 reference
profile and any training corpus. Neither a diagram nor a familiar map name
establishes current Steam geometry or loadouts.

## Motif experiments

| Motif | Proposed tactical purpose | Controlled change | Failure to look for |
| --- | --- | --- | --- |
| Raised interior walkway | A crossing or alternate approach above usable combat space | Change one walkway width or screen while retaining endpoints | Lower headroom loss, dominant rail sightline, one entrance, snagging at turns |
| Atrium | Shared landmark with several differently oriented approaches | Close one peripheral bypass | All recovery routes forced through its center; upper position holds every resource |
| Courtyard | Deliberate open encounter and orientation landmark | Change one approach screen with court size fixed | Excessive exposure, empty travel or an outdoor theme with no gameplay role |
| Horseshoe room | Two arms around a core with different peeks and a connecting crossbar | Open/close one arm's secondary exit | Two long dead ends, trivial camping of both mouths, unnecessary U-turns |
| Crate cluster | Break a view or offer a defined climb and landing | Move one crate or change one height | Unreadable climb, accidental cover gaps, blocked strafe lane, mandatory trick jump |
| Teleporter | Optional committed rotation between useful destinations | Enable/disable its trigger with ordinary geometry fixed | Compulsory travel, predictable arrival death, misleading exit facing |
| Shallow liquid | Alternate immersion/sound/movement commitment beneath dry circulation | Wet/dry basin with the same solid bottom and rims | Unexpected swimming, slow or obstructed exits, misleading surface, poor recovery |
| Deeper liquid route | An explicit underwater alternative with readable exits | Change one exit or air-pocket location | Drowning trap, no orientation, dominant exit ambush or hidden mandatory route |
| Interleaved elbow | Nearby routes turn around different parts of a shared structure | Move one inside barrier or turn station with destinations fixed | Valid endpoints joined by a blocked diagonal, inadequate passing room or false vertical connection |

Do not combine all motifs in every map. A generation portfolio should include
different combinations and meaningful absences. Fold uses an indoor atrium;
Lockyard supplies the separate open-sky courtyard and deeper swimming fixture.
It is larger than Fold and has lower gross envelope coverage, so the new motifs
are not evidence of a further packing improvement.

Ravel's 1,696×1,440 XY envelope has 18.1% less area than Fold's and 29.9% less
than Lockyard's. Gross room/passage union coverage is 94.0%, compared with 94.8%
and 91.0%. Its local levels, ramp, stairs and optional drop test a different
combination; smaller extents and higher coverage do not prove tighter player
flow or establish a quality threshold.

The Weft portfolio fixes nine requests (4/6/8 rooms × seeds17/42/99), captures
the previous default CLI outputs, and retains normal six-candidate selection
for the new policy. Declare the example before examining results: Weft is
rooms6-seed42. Keep optional display naming outside selected MAP/report content.
Two separate low/high verticality prompt controls test meaningful absence and
height response; they are not additional matched baseline pairs.

Initial revised compiled model-0 XY envelopes are 31.7–41.7% smaller, and all
nine pass local compiled hull/support and ordinary directed graph checks.
Nevertheless, rooms6-seed99 exposes two of 15 spawn pairs and rooms8-seed42 one
of 15, compared with zero in every baseline. Retain those initial revisions and
every analyzer's findings while refining the policy. Smaller envelopes, fewer
quality-owned findings and graph redundancy do not jointly establish safe
spawning or better matches. Full standing hull failures centered at baseline
pickup origins also remain bounded placement diagnostics, without a claim that
the pickup is unreachable in the native game.

After bounded spawn-location refinement, the nine revised outputs retain those
extents and have zero compiled visible spawn pairs with no local hull/support
or ordinary graph failures. Thirty-nine analytical lighting warnings remain.
Record that narrower improvement beside the initial regressions; do not erase
the original output or treat zero rays as a complete spawn-safety test.

## Record construction before judging flow

For each route retain the feet heights, collision hull, width, ceiling, endpoint
support and exact connection mechanism. Raised walkways need separate lower and
upper checks. A crate top is only a route if its ascent, landing and departure are
specified; a box that happens to be jumpable is not a proved ordinary connection.

Check the full elbow with the standing hull and continuous floor support, then
retain any failed diagonal shortcut. Reclamation's nested lower and upper paths
pass separately, but two `trigger_push` volumes elsewhere prevent a floor-plan
assumption from becoming an ordinary return certificate. At two stacked checked
stations, the intervening solid deck blocks direct descent. For Ravel, the ramp,
crossing and optional drop therefore receive independent compiled checks and an
explicit walk/step/door-only return policy.

For liquids retain contents, surface flags, bottom support, surface elevation,
depth, local flow/current flags, entry/exit profiles and air availability. Q2's
standing sample heights are feet +1, +23 and +46. A floor-0 player in water with
surface Z=24 has two samples submerged; do not label that ordinary dry walking.
The current ground graph conservatively excludes liquid volumes and has no swim
transition. Prove dry ordinary circulation separately from optional wet motion.

Trace emergence and exit as a complete route: submerged approach, full-hull
surfacing clearance, shallow section, lip/turn and dry recovery. In gladdm1 the
direct ascent proposal fails at the crest and the valid exit turns left. In
Lockyard, level-view native inputs stall beneath the low roof; downward-view
profiles pass with a full standing hull. Keep that failure beside the success
and test whether a new player can discover the required dive.

Close individual passages in the serialized directed ordinary graph by removing
their nodes, then recompute required journeys. One room node can conceal separate
dry stations around a wet section. Lockyard's new Court–west-station opening
removes the Boiler-door dependency; closing both openings in the final graph
loses 57 required pairs. This diagnostic is a graph intervention. A physical
closed-door variant still requires its own compile and movement tests, and an
optional single-exit health recess need not fail the design.

For courts, check sky-first rays at useful standing viewpoints, including those
beneath galleries or approach roofs. Record cover and open sky separately rather
than inferring all-point openness from a sky-textured ceiling.

Teleporters need their native solid pad and trigger, supported raised arrival,
facing and a useful next decision. A clear arrival hull does not establish safety
from opponents or guarantee successful activation. Audit occupied exits and
post-arrival momentum under the actual game module.

The [packing checker](../tools/map_recipes/packing_audit.py) computes an exact
union of authored room and passage boxes in XY, without double-counting overlap.
It partitions uncovered envelope cells into those connected to an exterior edge
and enclosed holes. It does **not** subtract internal cores or cover, infer usable
floor area, simulate movement or assign a quality threshold. Do not compare this
gross volume metric directly with the atlas's rendered-surface projection.

## Paired movement and match observations

Pin map, source, engine/module, assets, mode, gravity, movement configuration and
item rules. For route trials match starting position, facing, stance, velocity
and equipment. Record failures as well as successful trips. Separate traversal
time from waiting, pickup contact, finding the exit and reaching useful cover.
For teleporters add arrival orientation and occupied-exit outcomes; for liquid
add waterlevel, velocity changes, air and exit attempts.

The optional [Quake II 3.19 native harness](../tools/native_movement/README.md)
now supports a narrower world-only movement experiment without a client or input
device. Pin its external sources, compiler, BSP, settled state and command
stream. Match wet and drained solids, entities and input; report first accepted
standing/grounded/dry arrival at 16 ms resolution and retain failures. Across
Fold and Lockyard, the current 16 trials include 14 completions and two low-roof
stalls. The control removes water from the same route, so it measures that
intervention rather than the actual ordinary perimeter or fastest route choice.
Oxygen, damage, item/mechanism activation and opponents require a full game test.

The [native Talus pair](Q2_NATIVE_ASCENT_STUDY.md) adds 24 complete primary
journeys and eight deterministic repeats across three lanes, both directions and
two fixed inputs. Preserve its separate destination-plane and grounded-arrival
events: uphill ramp/stair times tie; downhill differences include recovery after
crossing. Track feet position, stored velocity and ground state together. A
planner's extra step costs cannot be called measured time, and one paired
fixture cannot determine a universal replacement weight. Human turning, jumping,
passing, camera/aim response and tactical route choice remain separate tests.

The six Ravel probes (`out/ravel/native-routes/README.md`, upstream study evidence, not imported) add fixed-input
center-ramp and ledge journeys on the final map. For the drop, compare the drawn
clearance candidate with the measured first landing and later grounded station;
the player retains momentum and does not land at the diagram's point. Successful
arrival is not proof of braking room, turning recovery or a harmless fall.

The later [native recovery experiment](Q2_NATIVE_RECOVERY_STUDY.md) retains five
successful grounded low-speed pauses and three failures across eight fixed Ravel
profiles. Releasing input after landing succeeds at both approach inputs; the
north-turn profile succeeds only at the slower input within the declared pause
area. Keep approach, first landing, continued motion and completed dwell
separate. These turn/pause results stop before onward doorway traversal and do
not constitute Weft movement or game-module damage tests.

Use 2, 4 and 6 players for original small FFA candidates. Review spawn recovery,
encounter intervals, congestion, empty travel, resource control streaks,
under-walkway use, upper-route use and peripheral bypass use. A smaller envelope
can increase unfair pressure or remove useful repositioning, so retain outcomes
that contradict the compactness hypothesis.

Client-based testing must use a non-hijacking windowed engine path. Game images must
come from the engine's screenshot command. Analytical atlas/viewer exports are
geometry diagrams and must not be presented as engine-rendered evidence.

Promote a motif into bounded experimental generation with a reproducible
construction contract, negative checks, scope controls and a fixed comparison
portfolio. The Weft policy takes that step for compact Q2 deathmatch/duel
crosswalks, galleries and barriers. Repeated native movement and player evidence
are still needed before claiming reliable full journeys or multiplayer quality.
Rule cards and examples do not train model weights; automatic selection is
established by the actual generator path and unmodified pipeline outputs.

## Second-family and whole-circuit follow-up

The [Splice iteration](Q2_SPLICE_STUDY.md) retains the same nine comparison requests and declares its separate rooms6-seed73 example before changing source. Keep both candidate families, every rank score and final canonical selection identity. Measure room-only slicing strips, canonical multigraphs, simple-room shortest paths and compiled envelope independently. Include a negative split-arm case whose aligned seam restores a complete slicing strip despite preserved graph cycles.

Retain the complete first native commands and failed route classifications. The [Weft circuit protocol](Q2_NATIVE_MOTIF_CIRCUITS.md) uses a narrower exit gate than the actual full-hull stair band; post-execution physical support observations must not replace those original outcomes. A subsequent geometry-bound gate or turn-timing experiment needs its own predeclared protocol and matched inputs.

The [Lattice ranking experiment](Q2_LATTICE_STUDY.md) freezes six candidates for each of nine primary requests, six separately declared holdouts and one predeclared example. Bind rank-neutral geometry hashes, feature vectors and baseline MAP/report bytes before comparing selections. Preserve exact feature definitions, weights, products, score ties and repair counts; test corrupted evidence. Report primary and holdout variety separately and retain larger envelopes or longer room routes. A deterministic tie rule supplies reproducibility, not learned preference or guaranteed tactical variety. For [native Weft turning](Q2_NATIVE_MOTIF_TURNING.md), retain historical0/8, protocol-only2/8 and new16/16 as distinct populations; use only the eight matched new rolling/pause pairs for timing comparisons. Their wall-adjacent finishing pockets do not test open-space stopping or Lattice movement.

## Three-stage construction and spawn controls

The [Braid experiment](Q2_BRAID_STUDY.md) freezes identical prompt texts at
baseline A, typed-intentB and spawn-refined C. Keep the nine primary requests,
six odd-seed holdouts and separate seed 503 example distinct. Thirteen requests
explicitly ask for wrapped construction; three four-room controls do not.
Compare A→B geometry and placements together, then isolate B→C with
spawn-neutral candidate hashes, selected compiled world signatures, unchanged
nonspawn entities and pair-specific eye/support checks. The 42-file unspecified
parity record is historical stageB evidence; the 12-file final CLI/legacy record
is a separate gate.

Retain close pairs, lighting findings, increased envelopes and reduced graph
variety. Source refinement must protect each previously blocked pair, accept
only strict objective improvement and validate initial room/floor plus full
standing support. Bounded nearest 24/two-pass search is reproducible local
placement, not an optimum or a learned spawn-count policy. Native selection,
first-resource journeys and opponent recovery need separate fixed protocols.

For reference resource circuits, declare stations and every edge before
checking them. Keep a failed drop failed even when the endpoints are supported
and all graph joins match, as in [The Rage](Q2_RAISED_RESOURCE_CIRCUIT_STUDY.md).
Use the same endpoints for comparative path lengths; label point rays,
full-hull support, static step constructions and native movement separately.
No Braid movement claim transfers from the earlier Weft trials.

## Upper links and layered support

For an inter-room upper route, freeze the existing decks, endpoint fragments,
ground accesses and lower doorways before extending geometry. Require a
continuous full-hull upper sweep and explicit support wherever a lower free
volume can erase an implicit floor. Protect every old stair's complete
raise-before-advance envelope. A physical upper closure must break the upper
transfer while the lower route retains support and clearance.

[Hinge](Q2_HINGE_STUDY.md) retains baseline, first construction and phase-refined
outputs for the same 16 requests. Keep all candidate offers and selected seeds,
including the first no-link hero and the remaining no-compatible cases. The
phase fix must pass translated exact-width and too-narrow controls, followed by
actual emitted-shell lowering; geometric tags alone do not establish coverage.
Keep allocation changes, lighting findings, graph descriptors and native
journeys separate. More room-graph edges can represent parallel floors rather
than shorter journeys or additional room adjacencies.

For native trials, use the actual selected connection, not an earlier feasible
proposal. Hinge's eight rotation profiles and four transfers retain one failed
phase gate, even though its later trace stays supported and reaches the goal.
Record exact command timing, all frame states and stopping-pocket geometry.
Three compiled normal/closed/missing-support worlds and a separate lower-route
audit make support dependence explicit; they do not execute lower return,
resource contact or gameplay.

## Teleporter commitment, arrival state and onward resources

Freeze source approach, destination/yaw, both physical pads, departure-room
bindings and resource endpoints before measuring. First test full standing-hull
arrival clearance and floor support. Then test each complete off-disc path,
including turns and the shared distance before branch choice. A passage
interior contact must not substitute for actually entering the next room.

Use actual game-module trigger dispatch for activation tests. Record the
immediate relink separately from the next movement update, hold expiry,
ground contact and stable off-disc endpoint. Include vacant arrival, a live
occupant, an unkillable obstruction, missing target and a trigger near-miss.
Keep host-initialization failures distinct from mechanism results.

[Relay's regimen](Q2_RELAY_STUDY.md#next-matched-tests) extends its four first
native trials with proposed matched pickup journeys and 2/4/6-player blocks.
Preserve discs when disabling touch, and isolate partition changes from
teleport activation changes. Record the existing long arrival sightline and
spawn exposure instead of treating compiled or native movement success as
proof of combat quality.

## Span: full-width upper and lower contracts

The Span protocol (`out/span-portfolio/protocol.json`, upstream study evidence, not imported) freezes 9 primary,
2 holdout and 3 flat requests before generation, with all six candidates
retained per stage. Compare actual room/lower-passage geometry and distinguish
the optional upper-link extensions from room-local decks. Audit every selected
bridge room across rotations and heights, including all stair caps, turns and
the lower aisle; checking only the displayed centerline is insufficient.

After the four first paused native upper journeys, predeclare lower returns,
192-height/other-rotation transfer and interior stopping targets. Match the
earlier crosswalk and opposed bridge with fixed resource endpoints. Proposed
2/4/6-player blocks record chosen approaches, wrong turns, weapon acquisition,
high-ground control and ascent/descent deaths. First failures stay in the
population. Projected packing, source graph connectivity, physical clearance,
native movement and observed combat each retain their own acceptance gate.
