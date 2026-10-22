# Relay: arrival choices around an offset partition

Relay is an original six-room Quake II multiplayer experiment with ten ordinary
passages, two local floors and one optional one-way teleporter. Its arrival sits
behind a full-height partition in a perimeter gallery. Leaving west leads toward
Elbow; leaving east leads toward Crossover and a chaingun beyond the corner. The
same weapon has an ordinary ground approach from Source Bay.

This iteration develops the arrival-space vocabulary and its testing contract.
It is an explicit fixed recipe, not a new default-generator policy or trained
model. Existing parser, semantic planner, candidate construction and ranker
sources are preserved in the pre-iteration snapshot (`out/relay-baseline/record.json`, upstream study evidence, not imported).
The interactive floor layout (`out/relay/index.html`, upstream study evidence, not imported) shows the complete map;
the [source recipe](../tools/map_recipes/relay.cpp) reproduces its construction.

## Lessons from the local archive

The [arrival study](Q2_TELEPORTER_ARRIVAL_STUDY.md) fixes one teleporter each in
Painkiller by Sten “ztn” Uusvali and Torn Glory by Dan “CardO” Shannon before
running its hull measurements. Torn Glory's two accepted geometric routes share
336 units before they branch. That is a property of these selected paths, not
proof that every escape shares this distance. Painkiller's selected westward
grenade-launcher approach clips a raised cap during a four-unit descent, although
the item is visible and its separate standing station is supported. The failed
route is retained rather than replaced by a passing waypoint.

These observations add three useful distinctions: landing clearance differs
from departure clearance; two eventual destinations can share a committed neck;
and seeing a resource does not certify reaching it. Relay tests an alternative
with departures on opposite sides of its pad and an offset resource approach.
No reference brushes, textures, map assets or external implementation are copied
into the original layout. Archive rights and attribution remain in the study.

## Spatial contract

| Space | Gameplay hypothesis |
| --- | --- |
| Arrival Gallery | A recessed landing with two unequal ways around its partition; the chaingun requires leaving the pocket. |
| Relay Atrium | A long lower lane and raised gallery offer different angles and an upper armor destination. |
| Crossover | A small central room connects the ground loop to a separate upper crossing. |
| Elbow | A wall attached to the perimeter makes the ordinary western approach turn. |
| Source Bay | The source pad is an optional side choice beside an ordinary route and separate rocket launcher. |
| Switch Hall | Unequal attached barriers separate the southern railgun and Mega approaches. |

The rooms occupy a 1,728×1,600-unit envelope. Their all-height room/passage
projection covers 94.22% of it; only 18,432 square units of uncovered projection
connect to an outer edge. This descriptor includes room interiors occupied by
walls, stairs and decks. It measures gross packing, not walkable area or quality.

Only Crossover and Relay Atrium carry the upper network at Z128. Each has eight
16-unit steps with 36-unit tread depth and 128-unit width. The 24-unit-thick decks
leave 104 units underneath. There is no extra lift or liquid intervention in this
experiment; the earlier [liquid studies](Q2_COURTYARD_LIQUID_STUDY.md) remain
separate evidence.

The stock source origin is `(-704,-624,24)`, with target arrival entity
`(-480,672,24)` facing east. The actual stock arrival player origin adds ten Z
units; its feet initially lie at Z10 over the destination disc's Z8 top. The
[mechanism notes](Q2_MECHANISMS.md) distinguish that physical disc from the small
source touch box.

The arrival route figure (`out/relay/arrival-routes.png`, upstream study evidence, not imported) isolates the partition,
both departures and the ordinary source-to-chaingun approach. It is an analytical
compiled-ground cut with upper deck outlines, not an engine screenshot.

## Declared paths and compiled evidence

The first design protocol (`out/relay/design-protocol.json`, upstream study evidence, not imported) preceded all
compilation and route checks. Typed validation found an overlapping upper spawn
and shotgun. Revision 2 (`out/relay/design-protocol-v2.json`, upstream study evidence, not imported) moves only the
shotgun 112 units along its existing deck. The first source and failure log
remain in `out/relay/revisions/v1/`; geometry, spawns, teleport coordinates and
the four route polylines did not change.

| Fixed path | XY length | Endpoints and scope |
| --- | ---: | --- |
| West departure | 720 | West off-disc station to Elbow through passage 1. |
| East departure | 1,024 | East off-disc station, past the chaingun, to Crossover through passage 2. |
| Ordinary source-to-chaingun route | 2,240 | Source approach to the same chaingun, avoiding the source disc. |
| Arrival-side chaingun approach | 560 | East off-disc station around the partition to the chaingun. |

The off-disc stations are 128 units east and west of the destination. Their
flat route certificates intentionally begin there; the native disc departure is
a different test. These unequal journeys are not shortest paths, equal-value
choices or measured time savings.

The [arrival auditor](../tools/map_recipes/arrival_audit.py) checks every complete
32×32×56 hull sweep, including elbows and passage seams. Exact convex floor-cap
unions cover the entire swept footprint. Both stock solid discs also participate
in route collision. Separate checks cover all 64×64 units of pad support, the
raised arrival hull, its two-unit geometric settle, exact native target/yaw,
typed resource identity and distinct passage bindings. All four declared paths
pass. The [12 synthetic controls](../tests/test_arrival_audit.py) retain missing
floor, intermediate obstruction, narrow disc support, raised ceiling and target
identity negatives; a floor-zero route through a real disc correctly fails.
Another [12 binding controls](../tests/test_arrival_bindings.py) exercise complete
audits and room-boundary cases. Review found and corrected two checker gaps:
a route ending inside a passage could count as an exit, and filtering world
contents could hide a non-SOLID ceiling. Departures now start in the arrival
room, cross incident passages and finish inside distinct other rooms; every
world brush must be SOLID before filtering. Relay's four paths still pass with
unchanged geometry and waypoints.

The BSP passes sealed compilation, full VIS and extra4 lighting with one bounce.
All 33 compiled gameplay anchors and 16 tread centers pass their local hull and
support checks. All 264 spawn-to-anchor pairs remain connected using walk, step
and door edges, with teleport, lift, jump and drop excluded. Removing any one of
the ten passage-node sets loses none of those pairs. That last check removes
graph nodes; it does not compile ten doorway closures or execute every journey.

The general verification (`out/relay/verification.json`, upstream study evidence, not imported),
arrival audit (`out/relay/arrival-audit.json`, written by `python -B -m tools.map_recipes.arrival_audit`) and
dedicated initialization (`out/relay/engine-smoke/engine-smoke.json`, upstream study evidence, not imported) bind the
same BSP. The dedicated test passes 16 checks and advances 40 scheduled server
frames after initialization, with no connected clients or packets. The
analytical viewer passes 18 floor/theme/width states. Its images are diagrams,
not engine renders.

## Exposure remains a test question

The chaingun is hidden from the selected arrival eye by world geometry; both
off-disc stations are visible. One of eight arrival-to-spawn rays is clear:
Relay Atrium spawn 8 at `(784,688,24)`, about 1,264 XY units away. Three of the
28 compiled spawn-eye pairs also have clear world sightlines. The typed report
retains nine visibility warnings and three lighting-contrast warnings, with no
structural, navigation, lighting or timing errors. A clear ray is not a measured
hit opportunity, and a blocked one is not an arrival-safety guarantee.

The [stock game-module tests](Q2_NATIVE_TELEPORT.md) pass all four first Relay
attempts: input 200 or 400, leaving east or west. Actual `ClientThink` source
entry triggers the teleporter, applies its hold and destination-relative facing,
settles onto the disc, then permits a departure and a grounded stop. Each stop
has nine consecutive low-speed end states spanning 128 ms. Final displacement
is 142.625–144.875 units from arrival; these XY centers lie on the first static
departure segment. The native journey ends there, before the onward route or
chaingun pickup. Commands use a synthetic 16 ms clock with separate stock game
frames; this is a private headless stock 3.19 host, not a client or human match.

Eight separate development cases observed their expected outcomes after a
versioned host import fix; eight earlier startup failures remain recorded.
The occupied-client case executes an actual telefrag, while the unkillable
blocker case transports the actor into a solid overlap. That negative control
shows why activation alone cannot certify a usable arrival. An independent
verifier rederives the 12 completed trials from 1,603 movement-command states,
and 23 synthetic controls exercise its protocol and outcome checks.

## Next matched tests

1. Compare teleport enabled/disabled while retaining both physical discs,
   geometry, items, timers and the same source start/chaingun finish. A separately
   hashed test-module toggle must disable touch behavior without removing the
   source entity. Record approach, activation, hold, disc exit, onward approach
   and actual pickup separately.
2. Compare this partition with an explicitly matched no-partition control,
   preserving all other geometry and entities. Begin with fixed observer poses,
   then measure which exits opponents can actually deny. Keep the distant
   arrival sightline in the recorded exposure matrix.
3. Run four matched blocks at 2, 4 and 6 players, rotating condition order and
   starts. Record chosen exits, wrong turns, arrival deaths within three seconds,
   telefrags, chaingun acquisition and repeated control of both departures. These
   tests are proposed, not human matches already conducted.

A future procedural feature can use a typed opt-in shortcut intent, bounded
arrival-pocket proposals and explicit no-fit results. Its ordinary bypass and
resource contract must survive item/spawn allocation. This original prototype
does not establish that policy's coverage across the default six candidates.

Reproduction uses the project script (`out/relay/reproduce.ps1`, upstream helper script, not imported), which writes
to a separate empty output directory and leaves released evidence untouched.
External engine sources, compiler binaries and stock game assets are local
read-only dependencies and are not included in the map package.

## Regression and retained task files

The first complete regression run (`out/relay/ctest-final.log`, upstream test log, not imported) passed 57 of
60 groups. Three older Python test commands failed to import the project's
`tools` package when invoked as files. Their CTest commands now use the same
module-discovery form as adjacent tests, without changing their assertions.
The second complete run (`out/relay/ctest-v2.log`, upstream test log, not imported) passes all 60 configured
groups, including the pinned Q1/Q2/Q3 compilers and GGUF planner checks.

The knowledge update (`out/relay/knowledge-update-record.json`, upstream study evidence, not imported) appends three
rules to the existing 54, preserves prior rule and experiment objects, and
records this stock game-module work separately from earlier world-only trials.

Automatic approval review rejected removal of the three new Relay scratch
directories and one Python cache with “blocked by policy.” The exact paths and
preflight are retained in the cleanup record (`out/relay/cleanup-record.json`, upstream study evidence, not imported);
no rejected path or parent was retried. The unrelated test-launch issue above
was fixed; no other unrelated repository defect was found during this iteration.
