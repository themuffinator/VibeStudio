# Map design playbook

Use this sequence to turn reference study into an original, testable Quake II multiplayer layout. The goal is useful decisions, reliable movement and recognizable spaces. More connections, smaller sightlines, taller rooms or higher texture diversity are not quality goals by themselves.

The [rule cards](../knowledge/q2/rules.json) describe mechanisms and counterexamples. The [study regimen](MAP_STUDY.md) defines evidence and partition handling; the [first study](Q2_FIRST_STUDY.md), [Switchback study](Q2_SWITCHBACK_STUDY.md), and [route diversity study](Q2_ROUTE_DIVERSITY_STUDY.md) supply observations and unresolved questions. Exact local reference provenance and the newer hypotheses are in the [route diversity research record](../knowledge/q2/route-diversity-study.json). These remain design hypotheses until controlled runtime and player evidence supports them. Transfer to other games, physics or single-player requires a separate review.

The [continuous movement study](Q2_CONTINUOUS_MOVEMENT_STUDY.md) adds supported ramp approaches and landings from Violent & Funky, Vertical Asylum and The Cake is a Lie. One lateral lane in Vertical Asylum has a confirmed convex-hull obstruction despite the visible ramp width. The later [drop/return study](Q2_DROP_RETURN_STUDY.md) supplies one complete geometric return in Violent & Funky; native damage and the other candidate's full return remain unverified. Their provenance records retain archive/author capacity conflicts, revision uncertainty and unreviewed training rights. These local observations do not authorize geometry copying, profile contamination or weight training.

The [courtyard/liquid study](Q2_COURTYARD_LIQUID_STUDY.md) adds a supported
horseshoe around Manhunt's court and a complete deep-basin exit in gladdm1. It
checks covered approaches, actual surfacing columns and the turn at the dry lip.
[Lockyard](Q2_LOCKYARD_STUDY.md) applies those relationships to original geometry
and adds separate native movement evidence; the archive routes remain static
geometry observations.

The [interleaved-route study](Q2_INTERLEAVED_ROUTE_STUDY.md) certifies nested
Reclamation paths while retaining blocked corner cuts and the solid deck between
two stacked stations. The [native Talus comparison](Q2_NATIVE_ASCENT_STUDY.md)
adds matched ramp/stair trajectories; uphill ties and different downhill landing
states prevent turning a geometric preference into an unsupported timing rule.

[Weft](Q2_WEFT_STUDY.md) now applies a bounded compact-room vocabulary through
the default Q2 deathmatch/duel generator. Its fixed before/after portfolio makes
integration measurable across requests, instead of inferring generalization
from one authored example. Model weights and the reference profile remain
unchanged; play quality still needs runtime and player evidence.

## 1. Write the play contract

Specify game module, mode, movement settings, item timers, ordinary versus optional advanced movement, and lower/nominal/upper player loads. Describe the intended experience in one sentence: for example, short rotations between two resource contests with a lower recovery route. Record which choices should distinguish this map from the previous examples.

**Produce:** a design brief, resource budget and versioned experiment record. **Failure signals:** capacity inferred only from spawn count; unclear game-module semantics; every seed promises the same room arrangement. Resolve conflicting archive and author capacity labels before treating them as test labels. For CTF, add team starts, flag routes and side-swapped opportunities; visual symmetry does not establish fairness.

## 2. Assign destinations and route roles

Name the main resource contests and a useful recovery destination before drawing rooms. Give each important journey a direct approach and a physically distinct alternative where warranted. Label why each route exists: quick contest, flank, withdrawal, equipment recovery, upper approach or exposed shortcut. Trace the return journey as well as arrival.

**Produce:** a directed destination graph and a passage schedule listing endpoints, height, width, movement type and tactical purpose. **Failure signals:** all journeys return through one dominant room; two apparent alternatives converge at the same choke; a decorative loop has no useful destination; maximizing item separation produces empty travel. Count connections only after checking their real approaches and movement directions.

Test passage dependencies on the directed ordinary navigation graph as well as
the room graph. Lockyard's two dry culvert stations share one room label but are
disconnected when swimming is excluded. Removing passage nodes exposed a
required-return dependency despite zero articulation rooms. Retain optional
recesses that deliberately have one approach; judge the destination and recovery
cost before demanding redundancy everywhere.

## 3. Shape unequal spaces and purposeful floors

Translate route roles into different playable proportions, openings and approach angles. Keep a wider space for lateral combat alongside tighter encounters and brief recovery areas. Offset entrances so the longest views and quickest routes do not all align. Use a common architectural vocabulary while varying tactical roles; random dimensions and arbitrary crookedness are not useful asymmetry.

For every second or third floor, write its purpose: destination, alternative approach, local overlook with counterplay, or return connection. Mark ascent, descent and the lower player's response. Prefer partial galleries, crossing routes and useful intermediate landings where they add decisions. An overpass must be checked independently above and below: projected overlap is neither a junction nor proof of clearance.

**Produce:** dimensioned plans by floor, at least one section through each stacked interaction, and a floor-purpose list. **Failure signals:** a complete room plan repeated at every height; nobody has a reason to visit a platform; only one safe ascent to a position controlling every exit; misleading paths hidden by an all-floors diagram. The Triple Towers is a reminder that repeated motifs can aid learning when the interactions work.

Keep one XY envelope and world-unit scale when switching between a map's floor
plans. Use a scale bar and subdued other-floor context to compare where routes
really overlap. Re-fitting each floor independently can hide a footprint change
or exaggerate a small ledge. Dashed context is an orientation aid, not current
floor support or a connecting edge.

### Compact rooms and an expanded spatial vocabulary

Use the [compact multiplayer regimen](MP_COMPACT_LAYOUT_REGIMEN.md) and
[reference pattern study](MP_SPATIAL_PATTERNS.md) when exploring raised walkways,
atriums, courtyards, horseshoes, crate routes, teleporters and liquid pathways.
Bring useful spaces beside and above one another with short offset connections.
Preserve sufficient movement and turning room; a tighter exterior envelope is
not permission to shrink every encounter or erase purposeful open space.

The local Quake Live orthographic atlas (`out/ql-atlas/index.html`, written by `python -B -m tools.ql_atlas.package`) compares six
pre-Steam revisions at fitted and common scales, with separate height bands and
authored item/mechanism annotations. These are projected render surfaces, not
walkable areas or current Steam loadouts. Keep QL spatial observations separate
from Q2 physics and resource rules.

A horseshoe needs its actual arms, core, crossbar and onward choices. A raised
walkway needs independent supported routes above and below. A crate needs a
declared role and, if climbable, a reliable ascent and landing; a local 64-unit
prop does not become a third whole-map floor. Liquids need depth, native contents,
surface flags and exit contracts, with dry required routes kept separate from
unimplemented swim navigation. Do not force every motif into every generated map.

For courts, trace sky from selected supported viewpoints and record covered
entries separately. An overhead gallery may preserve a useful route while
covering the lower eye. For deeper liquid, sweep the full hull from the bed to
an emergence pose, then continue through the exit turn and dry landing. A visible
water surface or a large ramp silhouette does not prove enough room to surface
or leave. Lockyard's bed is a local wet level, not a third whole-map combat floor.

## 4. Make control and recovery contestable

Place complementary resources across the route system. A strong position should cost something to maintain: departure for equipment, an exposed transition, a flank, or another measurable commitment. Test placing useful weapons or recovery below an upper contest so height does not automatically grant every benefit. Avoid making every low pickup visible to the same dominant position.

Audit each spawn's facing, cover, first useful weapon and alternative exit. Separate resource distance from access cost: a weapon close through a wall may be slow to reach. Keep some recoverable situations while allowing earned map control.

Use a declared sampling basis when evaluating cover. Q2 spawn-pair rays now use the entity origin plus 22 units in Z in both the typed visibility analysis and compiled spawn analysis. Reports retain actual sampled endpoints and label the offset; other placement roles retain origin sampling. Reject eyes inside solids or outside explicit free space. Historical origin-ray reports need a fresh matched-basis analysis before comparison. Neither eye rays nor zero visible spawn pairs establish safety from an opponent on another route.

**Produce:** item/spawn tables with stable IDs, floor and intended role; spawn-to-equipment journeys; a control ledger listing each strong position's benefits and counterplay. **Failure signals:** one safe lap gathers all rewards; two major items can be held from one position; recovery requires the same exposed door repeatedly; a high spawn begins with disproportionate equipment access.

## 5. Prove the movement contract

Build sealed geometry and validate intended paths with the target player hull and physics. Check stair treads, rises, landing support, headroom, narrow turns, trims, drop destinations and both travel directions. Separate ordinary required routes from optional skill moves. Movers and teleporters need explicit state, timing and exit tests. Preserve real bent or stacked passage geometry during repair.

**Produce:** editable source, typed layout, navigation report, spawn-to-gameplay-anchor reachability results and compiled hull/anchor checks. **Failure signals:** clearance vanishes after hull erosion; a landing supports only a point; lower geometry clips an upper route; a repair silently replaces an intentional path. The Switchback study records why the procedural generator and recipe helpers use 36-unit treads. `ConstructionBuilder::StairRunSpec` still defaults to 32, which leaves no positive-area origin cell in the core's per-tread erosion model. This is a conservative planner limitation, not proof that native Q2 stairs require treads wider than the player: the later `gladdm1` certificate traverses 24-unit treads using adjacent support and bounded step constructions. Audit aggregate stair support and API callers before choosing a default change. These dimensions are not universal movement rules.

For stock Q2, use the [native mechanism contracts](Q2_MECHANISMS.md) and [traversal study](Q2_TRAVERSAL_STUDY.md). A lift is authored at its upper station and starts lower only under the appropriate native activation rules. Record its full platform and standing-passenger sweep. A teleporter needs supported 64-by-64 discs, canonical trigger bounds, the actual raised arrival hull and explicit facing. Audit arrival threats independently from spawn pairs. Prove ordinary circulation with optional mechanism and jump edges disabled before judging shortcut value. An automatic lift's waiting and acceleration behavior cannot be inferred from height divided by speed.

For ramps, use the [continuous support contract](Q2_RAMPS.md). Retain the actual axial height function, complete upright hull, entry lip and high landing. Feet touch the highest plane under the hull, so an origin cannot simply use the surface height at its center. Talus's 120-unit rise over 480 units, above an 8-unit lip, gives a Q2 player's first fully supported feet height of 16 and final height of 128; the corresponding upper ramp gives 144 through 256. Export each area's full slope metadata, hull profile, local node origin, edges and placement bindings. A flat maximum-Z proxy or hidden staircase is insufficient. Core accepts only bounded axial wedges and conservative full clearance; the compiled checker independently checks the actual planes and continuous hull sweeps along explicit lateral lanes.

Name the exact ordinary-edge policy in every artifact. Talus requires all 264 spawn-to-gameplay-anchor journeys using walk, step and ordinary door edges only, excluding jumps, drops, lifts and teleporters. Counterweight's older ordinary policy also permits bounded drops; retain that distinction. A room graph with no articulation rooms cannot establish these hull-level returns by itself. Treat a drop as a separate directed commitment: prove its landing, exposure and full route back, then measure velocity-dependent damage under pinned gravity, water depth, module and deathmatch flags. A fixed drop height alone cannot be labeled harmless.

Record the return's burden, not just its existence. One demonstrated `gladdm1`
route travels 3,088 plan units, climbs 384 and descends another 112 after the
initial 272-unit drop. Its intermediate climb is hidden from one tested ledge
eye, while most of its final climb is exposed. Neither this selected route nor a
BFS return is automatically shortest or competitively viable. Undercut separately
checks two literal horizontal-then-vertical descent candidates and exact-height
typed ordinary returns. A clear two-leg translation is not a native fall path.

Certify the elbow as a complete trajectory. In Reclamation, the lower two-leg
turn is supported and clear while the straight diagonal between the same valid
endpoints hits two world solids. The upper route also requires an offset turn;
the closer-looking initial candidate fails. Compactness must preserve the
usable swept hull around an inside corner, then be checked under native turning,
passing and pursuit. For a drop, first leave the actual platform edge; two
supported floors at the same XY may have a solid deck between them.

## 6. Establish landmarks, materials and light

Give decision points recognizable views of local spatial identities. Vary ceiling profile, opening direction and landmark silhouette before adding texture noise. Define material roles for structure, floors, trim, hazards and landmark accents within one coherent theme. Light entrances, opponent silhouettes and relevant hazards while preserving the intended mood. Place typed lights in validated passage volumes where needed to illuminate corridor bends.

Treat Q2 material selection as a physics decision as well as art direction. Read each selected WAL's flags, contents and value, then inspect compiled collision contents for its intended role. Talus's initial `e2u1/mud1_1` palette entry had flags 8 (`SURF_WARP`) and contents 32 (`CONTENTS_WATER`): architectural surfaces still rendered, but compiled stair-support checks failed even though the typed solids passed navigation. `e2u1/mine04_1` has zero flags/contents/value and replaces that entry. Asset existence and a valid texture name are insufficient gates. Retain the failed material as a negative control; require a fresh compile and player-mask support checks after the replacement. Intentional water must keep its actual liquid contract, so globally erasing metadata is not a valid fix.

**Produce:** a small material/landmark schedule, representative camera positions and engine-written screenshots with display settings. **Failure signals:** every junction looks interchangeable; bright backgrounds obscure opponents; trim suggests a walkable ledge that collision rejects; an attractive landmark cannot be seen when a choice must be made. Texture entropy, light-entity counts and nonempty lightmaps cannot establish coherence or readability.

## 7. Validate in increasing levels of evidence

| Evidence level | Required artifacts | What it can establish |
| --- | --- | --- |
| Construction | Source/BSP hashes, exact commands/tool versions, compile/leak/VIS/light logs, structural and hull reports | A reproducible compiled artifact and the specific static properties tested |
| Initialization | Pinned isolated dedicated engine/module, map and entity diagnostics, scheduled ticks, a failing buried-item control | The tested BSP loads and emits no checked entity/item initialization failure; no player movement or rendered appearance |
| Native world movement | Pinned CM/Pmove code and BSP, settled start, command stream, complete frames, explicit goal and failures | The tested hull and inputs traverse the compiled static world; no game-module oxygen, damage, items, movers or opponents |
| Movement | Windowed engine traversal record, pickup contact checks, stair/landing/return-route results, mover/teleporter states | Whether the intended movement works under recorded runtime settings |
| Readability and performance | Engine screenshots; first-time recognition tasks; representative frame-time and busy-view measurements | Cue effectiveness and renderer performance for the tested settings and views |
| Play | Match settings, player familiarity, artifact hash, route/encounter/resource observations and failures | How the map behaved at a particular load and in the tested player group |

Use the engine's registered screenshot command for game rendering evidence. Do not substitute desktop capture. Follow the project's user-input permission rules before any controlled traversal or automation.

Windowed mode alone does not prevent input disruption. The installed normal Windows Q2REPRO client requests foreground/focus during startup; a non-hijacking startup/render path remains an engineering requirement before interactive validation. Dedicated initialization can establish load and checked entity behavior, but cannot stand in for a connected player, rendered screenshot, pickup contact or a movement trial.

The optional [world-only native harness](../tools/native_movement/README.md)
provides a bounded intermediate step without a client or input-device access.
Its Quake II 3.19 results use 16 ms commands and an explicitly settled standing
start. Fold's forward-400 crossing takes 1,680 ms wet versus 1,424 ms drained.
Lockyard's level-view inputs stall at its low roof, while a constant 30° downward
view at forward 400 completes in 6,400 ms versus 3,920 ms drained. These are
specific command profiles, not fastest times or a universal water-speed ratio.
The drained culvert is not the actual longer dry perimeter. Test that alternative
separately before claiming a tactical shortcut, and retain full-game air/damage
and first-time discoverability checks.

For matched ascents, record both the first destination-plane crossing and the
first usable grounded arrival. Talus's 24 native primary journeys all complete,
with uphill ramp/stair ties across three lanes at both fixed inputs. On descent,
forward-400 planar crossing ties while the stair landing completes 96 ms later;
forward-200 stairs cross 16 ms later and recover another 32 ms afterward. The
eight deterministic repeats reproduce command/frame bytes, rather than adding
eight independent human observations. Stored Z velocity can remain zero during
16-unit native step position changes. Position, velocity and ground contact must
therefore be read together; graph step penalties remain uncalibrated proxies.

Report lower, nominal and upper loads separately. Collect encounter intervals, congestion, empty travel, deaths soon after respawn, equipment acquisition, use of each floor and control reversals. Core costs include same-node position distance and cross-node endpoint approaches; slope costs also include local rise and travel through the proved ramp-end gates. They remain Manhattan/node-anchor proxies, not elapsed engine time or optimal continuous paths. Preserve the exact cost and sampling basis when comparing historical artifacts. An unverified AAS sidecar or point ray is not player navigation evidence.

Descriptive bands are prompts for investigation, not pass/fail quality limits. Combat-height bins are not floor counts, upward area includes trim, sampled LOS is not engagement range, and zero spawn-pair LOS does not prove safe respawns. Keep generated examples outside the reference profile. Hard construction failures cannot be offset by an attractive aggregate score.

## 8. Change one cause at a time

Keep a baseline, nominate one hypothesis and record the expected mechanism before making its variant. Preserve unrelated geometry, item counts, timers and settings. Use matched starts or side/player swaps where practical; retain failed variants and report uncertainty.

Audit the compiled difference as well as the source inputs. A stair/ramp swap
with the same typed material role can still invoke a horizontal-face texture
rule. The [Talus comparison](Q2_RAMP_STAIR_COMPARISON.md) checks actual brush planes,
contents, face materials and entity properties around its one support-profile
intervention. Geometry-dependent lightmap changes must be recorded; unchanged
light entities do not imply identical rendered illumination.

| Change only | Look for | Evidence against the hypothesis |
| --- | --- | --- |
| Close one bypass | Central crossings, interceptions and forced backtracking | The bypass adds travel but offers no distinct tactical use |
| Close one upper return | Floor use, retreat failures and control exchanges | The floor remains irrelevant, or only becomes a camping dead end |
| Move one weapon between validated lower/upper sockets | Recovery time and uncontested control circuits | The new placement strengthens the dominant player or makes a spawn excessive |
| Add one sightline interruption | Engagement ranges and weapon choices | It causes congestion or removes the map's only useful long-range contest |
| Change one landmark cue | First-time wrong turns and return recognition | Players still miss the decision or mistake decorative geometry for a route |
| Enable one lift or teleporter | Waiting, ascent/arrival exposure, ordinary-route use and post-arrival survival | The shortcut is compulsory, unused, or repeatedly traps recovering players |
| Bevel one corner | Peek angles, movement width, snagging and encounter choices | The change is cosmetic, removes useful fighting space or hides a route |
| Swap an equal-endpoint stair and ramp | Native travel-time distribution, aim disruption, pursuit and exposure with topology/items fixed | Smoother motion offers no useful choice, increases uncontested exposure or disrupts another route |
| Widen only a ramp landing or remove one trim intrusion | Full lateral hull use, missed turns, wall contacts and contested passage | The extra space has no observed use or removes a useful commitment |
| Open or close one optional drop | Landing damage, threats, chase outcomes and the complete ordinary return | The drop conceals a missing return, creates an unavoidable trap or has no useful tactical role |
| Remove only a water volume | Native time, waterlevels, entry/exit success and input sensitivity on identical solid geometry | The intended route stalls, requires undiscoverable input or makes an exit too costly |

Promote a rule only after a documented mechanism, counterexamples, paired experiments and repeated reviewer/runtime evidence across varied layouts. Evaluate a fixed prompt/seed portfolio so that improving one example does not hide repetition or regressions elsewhere.

## Current example sequence

Crosscurrent established an initial original blockout; Switchback introduced unequal spaces and selective stacked routes. [Spillway](Q2_SPILLWAY_STUDY.md) adds six distributed rooms, ten connections and an independent overpass/underpass. Its compiled static checks pass; runtime behavior remains untested. Read the example's final evidence record before citing any outcome, and compare its floor use and resource circuits rather than treating the connection count as an improvement score.

[Counterweight](Q2_COUNTERWEIGHT_STUDY.md) explores actual chamfers, L shaped spaces and optional native lift/teleport choices across three useful heights. Its static checks and dedicated initialization pass. It has a smaller overall extent than Spillway but longer sampled views and a more connected main hall; it therefore tests a different combat rhythm, not a proven general improvement. Its study defines a matched-start mechanism experiment, arrival-ambush checks and the remaining movement/rendering/play gates. The [iteration roadmap](../knowledge/q2/iteration-roadmap.json) keeps unresolved hypotheses separate from validated construction contracts.

Talus adds two real continuous ramps to six unequal rooms, nine connections, four stair routes and selective levels 0/128/256. Its authored navigation has 33 gameplay anchors, all 264 walk/step/door-only journeys and no articulation rooms; it has no lift or teleporter. Each ramp has separate bidirectional low-step and high-walk joins. This broadens the original vocabulary without establishing that a larger map, smoother ascent or another floor is better. Read the [Talus experiment record](../knowledge/q2/experiments/talus-v1.json) for the exact final compiled/material and initialization gates. The later [native pair](Q2_NATIVE_ASCENT_STUDY.md) supplies controlled ramp/stair journeys; drop trials, connected-player movement, engine rendering and match observations remain distinct requirements. The initial water-material failure demonstrates why passing authored navigation cannot replace compiled support evidence.

[Undercut](Q2_UNDERCUT_STUDY.md) adds three local Sluice floors, five unequal
encounters, eight passages, six stair returns and two optional descents. All 264
ordinary gameplay pairs remain connected with drops excluded. Its median sampled
view and straight-line weapon distance fall relative to Talus, while its extent
and upper-tail sightline increase. This tests a different distribution of space
and commitment; native movement and player outcomes remain pending.

[Fold](Q2_FOLD_STUDY.md) then explores compact adjoining encounters with an
indoor atrium, horseshoe, crate route and optional water/teleport choices.
[Lockyard](Q2_LOCKYARD_STUDY.md) changes the composition to an outdoor spine,
partial gallery, independently connected dry loop and deliberate submerged
commitment. Its envelope is larger than Fold's, its median sampled sightline is
shorter but its upper tail longer, and direct weapon distances increase. Sixteen
retained native water/control trials across these two maps contain fourteen
completions and two useful failures. That evidence strengthens the construction
and movement contracts without demonstrating superior multiplayer flow.

[Ravel](Q2_RAVEL_STUDY.md) brings six staggered rooms into a tighter envelope
with nine passages, three stairs, one ramp, an above/below crossing and a local
third-floor drop choice. Its XY envelope area is 18.1% smaller than Fold's and 29.9%
smaller than Lockyard's, while 94.0% gross room/passage coverage lies between
their 94.8% and 91.0%. These descriptors do not measure usable floor density or
combat quality. The supported elbow and exact return contracts matter more than
the appearance of a closely packed plan. Twenty-three selected CTest gates, 24 preview
states, 72 prior-map preview controls and 16 dedicated initialization gates pass;
human movement, rendered readability and multiplayer outcomes remain open.

Ravel's six native probes (`out/ravel/native-routes/README.md`, upstream study evidence, not imported) complete the
center ramp in both directions and the ledge descent at two fixed inputs. The
descent illustrates why a geometric landing point is not a predicted native
landing: forward momentum carries the hull farther and persists after contact.
Retain the actual landing and grounded station as separate events; test braking,
turning, interception and damage before calling the drop a reliable escape.

The subsequent [recovery study](Q2_NATIVE_RECOVERY_STUDY.md) tests that gap on
the frozen Ravel BSP. Five of eight fixed profiles achieve a low-speed grounded
pause; three fail. Releasing succeeds at both inputs, while one north-turn
profile remains on the floor but outside the declared pause area. Preserve
position, velocity, contact and dwell as separate facts. A clear arrival hull
or static clear doorway pose does not establish a complete turn through the
doorway, and these measurements do not transfer automatically to Weft.

## Test a default policy across a fixed portfolio

Declare requests, seeds, candidate count and example selection before reviewing
results. Retain the old pipeline's MAP/report/BSP and compile every new selected
output under the same conditions. The [Weft portfolio](Q2_WEFT_STUDY.md) fixes
4/6/8 rooms across three seeds and uses default six-candidate selection; its
six-room seed42 example is chosen in advance. Separate low/high verticality
controls probe semantic behavior without entering the matched comparison range.

Measure topology independently of coordinate hashes. Weft's three four-room
outputs share one unlabelled room graph, as do its three six-room outputs;
the eight-room set has two. Motif labels distinguish more arrangements, but
neither signature measures within-room movement or player tactics. Record this
limit when describing variety.

Retain all analyzer findings and score components. The initial compact policy
reduces all nine compiled XY envelopes but introduces visible spawn pairs in two
cases. This is a useful counterexample to equating tighter packing with better
spawn protection. A deterministic placement refinement must preserve supported
stations and be judged on the whole fixed set, including cases it worsens or
leaves unchanged. Do not reroll seeds or edit the exported example after default
selection to conceal a regression.

Keep descriptor denominators explicit: compiled world bounds, authored gross
room/passage coverage and player-accessible floor area are different quantities.
Check ordinary routes and individual passage dependencies, plus actual compiled
local hull/support stations. Do not equate exact-center pickup clearance with
native pickup reachability. The current allocator's spawn quantity still follows
requested player count; pairwise line of sight alone cannot validate spawn
variety, selection behavior or resource balance.

## Keep the center optional and audit what is selected

[Splice](Q2_SPLICE_STUDY.md) offers an unequal perimeter around a playable center, with an outer ordinary bypass. Removing a room from the adjacency graph tests dependency among remaining rooms; it cannot certify the physical upper/lower journey. [Reference routes](Q2_INTERLOCKING_TOPOLOGY_STUDY.md) show why actual corner clearance and shallow floor changes need their own contracts.

Compare offered candidate families against delivered maps. Three row and three wrapped candidates can still yield the same chosen family for every request. Here, eight-room candidate pools contain three room graphs but selected variety falls from two to one. Preserve the counterexample and investigate ranking separately from geometry.

Bind native acceptance to the question being tested. The [first Weft circuits](Q2_NATIVE_MOTIF_CIRCUITS.md) all fail a strict protocol; four nevertheless follow supported upper routes and remain in the full-width stair band. Keep precision-gate failure, partial edge support, broad endpoint arrival and final low-speed recovery as separate outcomes. Do not widen a checkpoint retrospectively or widen a map solely to repair the test.

[Lattice](Q2_LATTICE_STUDY.md) separates ranking from candidate construction. Before assigning a descriptor a quality preference, test equivalent room partitions and solid-piece splits, then test whether a real obstruction changes it. The [bounded controls](Q2_RANKING_FEATURE_STUDY.md) expose both sensitivities. Retain raw inputs and clipping, freeze candidate identities and report all selected structural tradeoffs. Removing two unsupported preferences and resolving ties reproducibly does not prove better flow; a selected map can regain graph variety while becoming larger or requiring more room hops. The [new Weft turn pairs](Q2_NATIVE_MOTIF_TURNING.md) likewise separate alignment from speed: pauses reduce final lateral offsets but add time, and all finishes occur beside walls. Preserve that boundary before transferring stopping claims to open spaces or another map.

## Separate requested structure, spawn stations and resource journeys

[Braid](Q2_BRAID_STUDY.md) makes wrapped construction an explicit typed request.
Declare the intended family before selection and report its tradeoffs: the
eight-room example shrinks, while several six-room envelopes grow and selected
graph variety falls. A requested structure does not need an invented quality
bonus, and unweighted room hops do not measure combat travel.

For close spawns, first validate the exact eye endpoints and full standing
stations. Preserve each pair's solid or shell occlusion while seeking a small
supported move in the same room and floor; do not reward extra distance once
the existing warning is cleared. [The bounded sample](Q2_SPAWN_SEPARATION_STUDY.md)
includes close, occluded reference pairs separated vertically. It does not
justify widening maps, changing spawn counts or treating every distance warning
as equivalent tactical danger. Test first-resource access and recovery next.

A supported resource position and a visible route drawn in projection are
different claims. The [Rage approach study](Q2_RAISED_RESOURCE_CIRCUIT_STUDY.md)
passes two armor approaches from different starts but retains a blocked drop
in the proposed circuit. Check every full-hull edge and exact join before
claiming a closed route. Measure viewpoint exposure separately from support,
native movement and pickup contact; a supported Rail or Mega station cannot
repair its failed approach.

## Continue upper routes into other spaces

A raised route earns its height by connecting useful destinations. The
[Hinge construction](Q2_HINGE_STUDY.md) extends two existing decks across a
shared face and retains ordinary access in both rooms. The
[Powertrip comparison](Q2_UPPER_CONNECTION_STUDY.md) shows two supported
inter-space routes in one footprint with different fixed-station exposure.
Their distinct Z endpoints prevent treating their lengths as interchangeable
travel costs, and shared XY does not create a vertical connection.

Bind support to actual floor geometry, clearance to the complete standing
hull, and return access to the exact endpoint fragment. Preserve step-up
sweeps under new overheads, not merely clear poses on individual treads.
Retain failed strips and no-link candidates; coordinate phase, shell thickness
and selection can explain a missing feature without justifying a seed reroll.
Then test complete journeys and resource use before calling the added route a
combat improvement.

The [first native Hinge rotation](Q2_NATIVE_UPPER_LINK.md) shows why a supported
trajectory can still fail a predeclared turn gate. Preserve the failed profile
and its later arrival separately; define any revised gate before a new trial.
Seven passing rotations and four transfers do not establish human route
preference, and finishes beside walls do not establish open-space braking.

## Give teleport arrivals an onward contract

Declare the actual native arrival pose, its facing/hold and the physical disc
before placing resources around it. Then separate the off-disc departure,
shared route before branching, and approach to each destination. In the
[reference arrival study](Q2_TELEPORTER_ARRIVAL_STUDY.md), a visible grenade
launcher has a supported endpoint but a failed chosen approach; two other
accepted departures share 336 units before they diverge. These are local
observations, not universal distance targets.

[Relay](Q2_RELAY_STUDY.md) places a partition between arrival and chaingun,
with two ground exits into different rooms. Full-path checks bind real room
endpoints and include the native discs. Stock game-module trials separately
test contact, hold, facing and grounded off-disc recovery. Keep resource
acquisition, arrival exposure, telefrag outcomes and human exit choices as
separate measurements. An enabled/disabled comparison must preserve the
physical pads; removing a teleporter entity changes collision as well as
transport behavior.

## Opposed stairs, joined decks and separate lower routes

[Span](Q2_SPAN_STUDY.md) moves one approach to the opposite end of a raised
crossing while preserving a standing lower aisle. This changes a journey inside
an existing room envelope. Verify complete stair widths, landing joins, upper
turn squares and the lower route separately; long rooms and aligned landing
bands must be explicit construction cases.

The [atrium reference](Q2_ATRIUM_ROUTE_STUDY.md) retains a supported high route
whose final bend clips geometry. Its fixed cross-floor eye rays are blocked.
Neither projected overlap nor supported endpoints establishes an exposed,
traversable cycle. Span's four native upper journeys pause at corners and stop
beside walls; they do not include the lower return. Match resources and starting
stations before testing whether opposed approaches improve attack, escape or
control under opponents. Preserve all previous failed routes and native trials.
