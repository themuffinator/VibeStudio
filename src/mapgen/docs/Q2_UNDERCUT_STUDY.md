# Undercut — drops with ordinary returns

Undercut is an original Quake II deathmatch experiment for an intended 4–6
players. Five unequal spaces have eight passage connections and six stair runs.
The central Sluice uses three occupied floors at 0, 128 and 256 units; neighboring
rooms use two. Two optional 128-unit descents offer different downward choices.
All required circulation remains possible without drops or special movement.

The [recipe](../tools/map_recipes/undercut.cpp) is explicit project construction,
with no copied archive geometry or bundled game assets. The
[experiment record](../knowledge/q2/experiments/undercut-v1.json) binds the final
source, BSP, tools and evidence. This expands the construction vocabulary and
knowledge base; automatic motif selection and model-weight training remain
separate work.

## How the spaces work

| Space | Height and intended combat role |
| --- | --- |
| A · Sluice | Rocket/Mega below, a middle circulation rim, and upper armor beside the Crown approach. Two differently oriented stairs give ordinary returns; two ledges descend to distinct lower positions. |
| B · Cistern | Super-shotgun below, hyperblaster on a partial gallery. The lower baffle and upper C-shaped circulation give different approaches toward Sluice, Crown and Return. |
| C · Crown | A wide middle rail encounter divided by a tall offset screen, with grenades on an upper gallery. An upper screen interrupts the spawn view along the elevated Sluice connection. |
| D · Tailrace | An actual L-shaped footprint, lower armor, and a middle shotgun recovery route joining Crown and Return. The filled northwest corner changes movement space rather than only its outline. |
| E · Return | Lower Quad pocket and upper chaingun rotation. Stairs and west/east entrances offer different approaches; the ground passage returns toward Sluice. |

The rooms have distinct proportions and staggered entrances rather than paired
mirrors. The Sluice has four incident passages; the other rooms have three each.
Cycle rank is four and there are no articulation rooms. Those graph facts do not
establish traffic balance. In particular, three useful Sluice floors could still
concentrate control despite viable peripheral circulation.

The 128-unit floor separation leaves 104 units beneath each 24-unit gallery.
This keeps lower fighting space while making the overhead structure relevant to
views and movement. Third-floor area is local, not a repeated full floor plan.
All six stairs use eight 36-unit treads with 16-unit rises. Passages are 160 wide
and 192 high before local gallery intersections.

## Two commitments, two returns

Coordinates below specify player feet, not entity origins. A standing Q2 origin
is 24 units above the feet and its eye is another 22 units above that origin.

| Candidate | Takeoff → departure → landing | Ordinary return evidence |
| --- | --- | --- |
| Upper ledge | (176,248,256) → (112,248,256) → (112,248,128) | 12 graph edges, including the north stair, return from the middle rim to the upper ledge. |
| Mega pocket | (−64,240,128) → (−64,176,128) → (−64,176,0) | 10 graph edges, including the west stair, return from the lower pocket to the middle rim. |

Takeoff positions have their complete 32×32 footprints on their ledges. The
departure positions fully clear the ledges before the vertical clearance test.
Both landing hulls have full compiled flat support and 56 units of standing
clearance. Point eye rays from the takeoff positions to the landing positions
are clear. That is a limited readability descriptor, not a rendered view or proof
that an opponent cannot control the landing.

The independent audit tests the literal horizontal-then-vertical translations
against every compiled world brush in the Q2 player collision mask, including
the ledge itself. These are geometric candidates, **not ballistic trajectories**:
native acceleration, momentum, air control, step execution and fall damage are
not simulated. A 128-unit descent is not labelled harmless.

The return search binds both endpoint heights exactly to the serialized typed
navigation graph, without nearest-node snapping. It allows walk, step and door
edges only, excluding jump, drop, lift and teleporter edges. Its whole return has
not separately received a compiled continuous sweep certificate; compiled stair
support/clearance samples and the core's typed transition checks are additional
evidence, not a substitute for that missing proof. The reported edge counts are
not journey times or shortest continuous route lengths.

## Learning from an actual reference return

The [drop/return study](Q2_DROP_RETURN_STUDY.md) follows a concrete route in
Violent & Funky (`gladdm1`). Its earlier 272-unit descent candidate now has a
demonstrated ordinary geometric return: down the interior stair, across the
crosswalk, up the opposite stair and west ramp, across the north connector, and
up the east ramp to the armor shelf. The chosen return is 3,088 units in plan,
with 384 units of ascent and another 112 of descent. It is one verified route,
not a claim of the shortest return.

That example changes the rule from merely “a return exists” to “inspect what the
return asks the player to do.” Its middle climb is hidden from the tested ledge
eye, whereas most samples on its final climb are visible. Connectivity therefore
coexists with alternating concealment and exposed re-entry. Undercut explores
shorter local stair returns, but their combat value must be observed rather than
inferred from fewer edges.

The [controlled ramp/stair comparison](Q2_RAMP_STAIR_COMPARISON.md) is a separate
experiment on Talus's Bench. Its paired MAP/BSP package (`out/talus-stairs/qmg_talus_ascent_pair.zip`, upstream study evidence, not imported) preserves both conditions for the movement trial. Keeping this single-intervention comparison separate
from Undercut avoids attributing the effects of changed rooms, resources and
topology to one traversal shape.

## Current evidence and tradeoffs

The delivered sealed IBSP38 contains 178 SOLID world brushes. Full VIS and final
`extra4` lighting with one bounce and soft shadows complete successfully. All
33 gameplay anchors pass 5,874 hull-versus-brush checks, and all 48 stair centers
have compiled support and standing clearance. The four drop translation segments
pass 712 world-brush pair checks; their swept AABBs are already disjoint from
solid interiors, so these candidates require no narrow-phase SAT calls.

All 264 spawn-to-gameplay-anchor pairs pass with only walk/step/door edges;
Undercut has no door entities. Typed structural, navigation, visibility, analytical
lighting and timing findings are zero. All 28 compiled spawn-eye pairs are
occluded, with no invalid eye positions. These tests do not cover every attacker
position, spawn recovery outcome or dynamic actor.

| Descriptor | Talus | Undercut |
| --- | ---: | ---: |
| Rooms / passages | 6 / 9 | 5 / 8 |
| World diagonal | 2,793.5 | 2,864.4 |
| Sampled sightline median | 228.0 | 172.7 |
| Sampled sightline p90 | 561.2 | 585.9 |
| Straight-line spawn-to-nearest-weapon median | 320.4 | 268.2 |
| Visible spawn-eye pairs | 0 / 28 | 0 / 28 |

Undercut reduces the median sampled view and straight-line weapon distance while
retaining some longer views and a slightly larger overall extent. The result is
not uniformly tighter, nor a measured recovery improvement. Multiple variables
changed. Planner costs and direct distances remain proxies until native journeys
and matches establish actual times and control patterns.

The eight affected CTest gates pass: all six recipe layouts and the geometry/drop
audit suites. The isolated dedicated Q2REPRO smoke test passes all 16 loading and
initialization gates over 40 scheduled legacy 10 Hz frames after two settling
frames. There are no connected players. It does not test drop execution, pickup
contact, rendering or match balance. Exact logs and analytical viewer checks are
listed in the experiment record.

## Next paired play tests

1. **Drop availability.** Compare both ledges open, only upper open, only lower
   open and both closed. Keep items, spawn state, ordinary stairs and all unrelated
   geometry/settings fixed. Each closure needs its own compiled and native check;
   record the blocker itself as an unavoidable visibility intervention.
2. **Landing information.** With one ledge open, change only a small landing
   screen. Use matched attacker/defender starts and measure advance visibility,
   hesitation, interceptions, damage and successful disengagement. Do not confuse
   visibility from one fixed eye with general landing safety.
3. **Full commitment cost.** Measure takeoff-to-landing, landing-to-useful-cover,
   pickup contact and the complete ordinary return separately. Record actual
   fall velocity, damage, gravity, water state, module and `dmflags`. Include
   unsuccessful attempts and occupied landings.
4. **Resource control.** At 2, 4 and 6 players, record Sluice upper-armor/Mega
   ownership streaks, Quad recoveries, spawn-to-weapon times, repeated deaths and
   peripheral route use. A viable stair route may still be too costly under fire.
5. **Orientation.** In a windowed engine session, collect images using the engine
   `screenshot` command at both takeoffs, landings and return-stair entries. Test
   whether unfamiliar players identify the landing and way back without the plan.

The public 32-unit default in `StairRunSpec` and normal Windows client startup
focus behavior remain engineering issues outside this recipe. Undercut uses
explicit 36-unit treads and a dedicated initialization harness. Native movement,
rendering, competitive balance and default-planner motif integration remain open
in the broader roadmap.

The new reference certificate also clarifies the stair issue: 24-unit treads can
provide an ordinary geometric route through neighboring support and bounded step
transitions. The core's separate-tread erosion limitation is not a native minimum
tread-width rule.

Automatic approval review rejected temporary test-folder cleanup as “blocked by
policy”. No deletion executed or retry occurred; the retained project-local paths
are recorded in `out/undercut/cleanup-status.json`.
