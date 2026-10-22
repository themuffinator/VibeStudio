# Span: opposed stairs over an independent lower route

Span brings a new room pattern into the ordinary Quake II multiplayer generator.
Two stairs approach opposite ends of a raised bridge, while a wide lower aisle
passes underneath. The six-room example wraps unequal galleries around this
central crossing. It retains the existing lower connections and uses a separate
upper passage into its southern gallery. The floor layout (`out/span/index.html`, upstream study evidence, not imported)
shows the actual exported geometry.

This changes procedural construction, without changing the parser, semantic
planner, candidate seeds or ranking policy. It does not train model weights.
The fixed 14-request protocol (`out/span-portfolio/protocol.json`, upstream study evidence, not imported) preceded
generation; the displayed example was declared as six rooms, seed 701 before
any modified candidate was examined. Its selected candidate remains
15755400384260044540.

## What the reference study contributes

The [atrium route study](Q2_ATRIUM_ROUTE_STUDY.md) examines The Killing Machine
by Sten “ztn” Uusvali. Two independently checked stairs connect floor 0 to 128,
and a lower route joins their different bottom endpoints. Together these form
a 1,248-unit XY journey between the middle landings. The selected high route
at 288 fails its final bend despite complete floor support; the proposed 128
connection also fails. All 18 fixed eye rays are blocked. These observations
distinguish physical overlap, support, complete traversal and cross-floor views.
They do not establish an exposed atrium or a certified connection to floor 288.

The original Span bridge develops that distinction: certify upper and lower
routes separately, then examine actual exposure. Reference brushes, textures
and architecture are not copied. The archive remains read-only and excluded
from training admission. The existing 64-view Quake Live atlas (`out/ql-atlas/index.html`, written by `python -B -m tools.ql_atlas.package`)
continues to provide broader projected-layout comparisons; this iteration adds
a bounded Q2 collision study, not a new QL dataset.

## Construction and spatial intent

Every third eligible elevated room in the existing compact Q2 DM/duel family
receives `opposed_stair_bridge`. Eligibility remains within the existing
2–32-room construction scope; fewer than three elevated rooms can offer no
instance. Other rooms retain their earlier crosswalk, horseshoe and barrier
patterns. The new constructor consumes no random draws. Room order, bounds,
heights, rotations and lower passages remain unchanged. Item/spawn allocation
and optional upper-link placement can respond to the changed decks.

Each bridge uses two 128-unit-wide stairs, 16-unit rises and 36-unit treads.
Three 160-unit-wide connected deck sections stand at 128 or 192; their 24-unit
thickness leaves 104 or 168 units beneath them. Opposite stair approaches create
a different upper journey from the old same-edge arrangement. The middle deck
can form a bend in plan; aligned landing bands produce a straight bridge and
are valid cases. The lower center aisle remains full-width and standing-clear.

A pre-generation construction review caught an inverted middle interval in
long rooms and insufficient shared width at another dimension. The corrected
interval spans the union of both landing bands. Minimum, long and aligned
rooms are tested across all four rotations and both heights. The pure helper's
minimum ceiling check is a standing-geometry contract; the compiled stair
auditor separately checks the full 18-unit step-up envelope.

Span itself has six rooms, ten passages, ten stair runs and two principal
floors at 0 and 128. Its central bridge, two horseshoes, two crosswalks and
ground barrier bay provide different local shapes. Nine ground passages and
one upper passage connect the adjoining rooms. The 2,176×2,256-unit envelope
is unchanged by the intervention. Its room/passage projection occupies 95.15%
of that rectangle. This includes internal solids and measures gross packing,
not walkable area or improved combat flow.

## Fixed-population comparison

All six candidates are retained for each request, before and after the change.
The compiled comparison (`out/span-portfolio/comparison.json`, written by `python -B -m tools.mp_portfolio.span_compare`) binds their
source observations and every selected export. No candidate is rerolled.

| Group | Requests | Candidates per stage | Selected maps with bridges | Warnings before → after |
| --- | ---: | ---: | ---: | ---: |
| Primary, 4/6/8 rooms | 9 | 54 | 9 | 28 → 30 |
| Holdout, 5/7 rooms | 2 | 12 | 2 | 10 → 9 |
| Low-verticality controls | 3 | 18 | 0 | 23 → 23 |

Every high-verticality candidate offers the feature; all 14 selected seeds stay
unchanged. All three low-verticality MAPs are byte-identical. All 28 selected
before/after worlds pass generic compiled checks. The three unchanged worlds
reuse independent byte-identical BSP copies and receive fresh inspection;
25 worlds are newly compiled with full VIS and extra4 lighting.

The first comparison incorrectly treated optional upper-link extension slabs
as unchanged room-local architecture. Its failure and diagnostic remain
recorded. The corrected checker independently binds the final three extension
slabs to the actual upper passage and original deck faces before excluding
them from room-local conservation. Additional controls reject incomplete
candidate populations, wrong selection and stale/misbound exports. No map
geometry or frozen generation record changed to satisfy that checker.

## Physical and native evidence

The [independent bridge audit](Q2_SPAN_ROUTE_AUDIT.md) passes all 14 bridge rooms
across the 11 selected high maps, covering all rotations, thirteen 128-height
rooms and one 192-height room. Full stair caps, step envelopes, deck joins,
turns, lower aisles and 56 actual supported gameplay stations are checked
against the compiled world. These checks are bounded to static all-SOLID
geometry and do not simulate item contact or combat.

Span's generic inspection passes all 23 gameplay anchors, 80 tread centers and
138 ordinary spawn-to-anchor graph pairs. All those pairs survive removal of
each individual passage-node set. That is graph redundancy, not ten physically
blocked doorway simulations. None of its 15 spawn-eye pairs has a clear world
ray. Six lighting-contrast warnings remain. Six weapon types are present, but
the current allocation places no major item; resource economy and respawn
diversity remain design questions.

Four first world-only native trials complete the central upper journey in
both directions at inputs 200 and 400. Each ascends, crosses both bridge turns,
descends and reaches a grounded stop. All six ordered gates must pass; late
arrival cannot erase an earlier failure. The 2,306 retained 16-ms states include
four deliberate 24-frame corner pauses per trial. Completion is 8,624 ms at
input 200 and 6,192 ms at 400, excluding initial settling and including the
corner pauses. The final stationary dwell is wall-adjacent, so these are not
open-space braking or running combat-flow tests. Descent includes airborne
states. The lower route is statically certified but was not traversed back to
the start by the native actor: a geometric cycle is not a completed native loop.

Sixteen dedicated initialization checks pass after 40 scheduled server frames,
with no clients or packets. The analytical preview passes 18 floor/theme/width
states. Its images are diagrams, not engine screenshots. No mouse or keyboard
input, fullscreen client or operating-system game capture is used.

## Follow-up regimen

1. Freeze native lower returns and complete upper/lower loops at 128 and 192,
   including other rotations and long/aligned rooms. Preserve failed attempts.
2. Compare earlier crosswalk and opposed bridge with matched rooms, resources,
   starts and destinations. Separate continuous turns from paused movement and
   require an interior stopping target for braking claims.
3. Use actual resource pickup and opponent poses to test whether the bridge
   supplies useful attack/escape choices. Sample under-route exposure and
   whether one position can deny both stairs; do not infer this from height.
4. Run matched 2/4/6-player blocks, rotating layout order and starts. Record route
   choice, wrong turns, first weapon time, repeated high-ground control and
   deaths during ascent/descent. These human tests remain proposed.

The knowledge update appends three scoped cards to the preceding 57 and keeps
all old rules, experiments and Relay game-module records intact. The ordinary
CLI reproduces Span's MAP/report exactly; Q1/Q3 DM and Q2 SP/CTF controls also
remain byte-identical. An older nine-room Q2 DM control changes as expected
inside this intervention's scope. The first overbroad parity failure remains
recorded; classification uses the same first exports with no rerun.

All 64 configured CTest groups pass in the full regression (`out/span/ctest-final.log`, upstream test log, not imported),
including the pinned Q1/Q2/Q3 compilers and GGUF planner. Automatic approval
review rejected one removal request for the two new Span runtime folders and
three cache targets as “blocked by policy.” Their
exact paths and preflight (`out/span/cleanup-record.json`, upstream study evidence, not imported) are retained; no
rejected path, parent or alternate deletion method was retried.

The [released experiment](../knowledge/q2/experiments/span-v1.json) binds these
results and the useful retained logs. The historical
archive metadata line-12 failures and existing lighting/material/respawn issues
remain open; this iteration found no additional unrelated production defect.
