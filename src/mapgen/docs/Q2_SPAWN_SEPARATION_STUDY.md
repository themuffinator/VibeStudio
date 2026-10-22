# Compact spawn placement: local spacing with occlusion preserved

The frozen Lattice portfolio has five close unordered spawn pairs, reported as
ten directed `insufficient_separation` warnings. Every pair is on the ground
floor across adjacent room seams,176–224 units apart. Their source and compiled
eye rays are already blocked. The previous local refinement immediately skipped
spawns with zero visible peers, so it never addressed these spacing findings.

This experiment keeps the existing256-unit warning and requested-player spawn
count. It changes local placement priorities within existing rooms and floors;
it does not enlarge maps or establish a new universal safe spawn distance.

## A bounded reference comparison

Six maps were selected from the existing reference curriculum before inspecting
this sample's pair measurements. They belong to six recorded author/family
groups. No validation/test partition was opened. Each local BSP hash was checked
against its inventory record, then embedded stock-DM entities were read afresh.
The `.ent`/AAS sidecars are not substituted for runtime evidence.

| Reference | Authored DM spawns | Metadata player range | Closest pair | Pairs below256 | Visible eye pairs |
| --- | ---: | ---: | ---: | ---: | ---: |
| The Rage, Sten “ztn” Uusvali (`ztn2dm3`) | 8 | 2–6 | 204.900 | 1 | 0/28 |
| Powertrip, Dennis “headshot” Kaltwasser (`ptrip`) | 4 | 2–5 | 510.247 | 0 | 0/6 |
| Aerowalk, Mattias “Preacher” Konradsson (`aeroq2`) | 6 | 2–6 | 271.529 | 0 | 0/15 |
| The Triple Towers, Martin “Killer” Kilcoyne (`mktriple`) | 10 | 2–6 | 256.000 | 0 | 9/45 |
| The Chastity Belt Duel, German “JaLisK0” Garcia (`q2rdm2`) | 10 | 2–4 | 252.602 | 1 | 1/45 |
| Bio Rust, kold (`koldduel1`) | 6 | 2–6 | 241.727 | 1 | 0/15 |

The three reference pairs below256 are all occluded and have192 units of authored
vertical separation. The ten visible pairs occur in two other maps. These
observations separate count, Euclidean spacing, height and visibility. They do
not justify rejecting every short pair or every visible pair as poor design.
The metadata ranges are recommendations retained by the archive, not measured
capacity. Four to ten spawn alternatives for these maps do not establish a
new universal spawn-count multiplier.

All eyes in this sample are valid for the stated point-ray query. The basis is
authored origin+22, before native spawn adjustment. Model0 brushes with contents
mask27 block: solid, window, lava and slime. Water and entity submodels are
excluded. Reference standing hull support, native settlement, facing, enemy
approaches, weapon access, immediate deaths and human recovery are unmeasured.
The malformed-metadata warnings already present in all six inventory records
remain recorded. No reference geometry, entities or external code is copied
into the generator, and model weights/profile/rights decisions remain unchanged.

## Declared local policy

The reusable [helper](../src/spawn_refinement.cpp) accepts candidate origins but
validates them independently. This first scope is marked, matching2–8-room Q2
deathmatch/duel layouts with static all-solid AABB architecture. All room and
passage volumes must begin on one common shell-floor plane; a lower overlapping
free volume cannot silently erase the assumed floor. Trigger/liquid materials,
invalid ownership enums, convex bounds, liquids, movers and other mechanism
roles are rejected. Explicit layout intent also passes the shared intent guard.
The existing9–32-room
visibility-only path is retained by integration; this distance preference is not
transferred to those larger cases.

Each spawn retains its initial room and exact originZ/floor. A candidate requires
an entirely clear32×32×56 standing hull, complete support on the room floor or
one static box top, and at least64 units from every other non-light point
placement. Support across arbitrary unions and slopes is outside this helper.
The generator supplies its existing64-unit station grid: ground stations inset64
from room edges and upper stations inset32 from individual platform edges.
Candidates are deduplicated, ordered by current-station squared displacement and
XYZ, then capped at24. There are at most two passes in placement-ID order.

Each endpoint receives a separate zero-length exact-visible query. For the
validated static box construction, an exact `outside_free_space` **segment**
between valid endpoints crosses an implicit room/passage shell and counts as
shell occlusion. This is kept distinct from an explicit-solid hit. It does not
make an outside endpoint valid or alter the visibility analyzer's enum globally.
Conservative results and invalid endpoints are rejected. Both exact occlusion
forms are protected per pair: a move cannot exchange one newly exposed pair for
another hidden pair while merely preserving the total.

Eligible moves lexicographically minimize:

1. The number of visible unordered spawn pairs.
2. The number of unordered pairs with squared authored-origin distance below65,536.
3. The sum of `max(0, 65536 − squared_distance)` over those pairs.

Only strict improvement in this three-part objective permits a move. Equal
objectives prefer the least current-station displacement, then XYZ. Pairs
already at or above256 contribute no spacing reward, so the search has no
incentive to spread acceptable spawns farther. Zero-visibility, zero-close-pair
stations skip candidate ray tests. Other placements, counts, room assignments,
angles, geometry and thresholds remain unchanged. This is bounded coordinate
descent over a finite grid, not an optimum or a gameplay balance model.

## First in-memory feasibility control

The protocol (`out/spawn-separation-study/feasibility-protocol.json`, upstream study evidence, not imported) was written
before executing the five finite cases. Its helper hash and frozen input hashes
are retained. The independent prototype uses compiled world point rays and
rechecks the resulting complete standing footprints against the unchanged BSP.
It does not call the source visibility analyzer or regenerate a layout. Its
five inputs are the historical Lattice selections, before the separate wrapped
layout-intent intervention; they must not be conflated with that later stage's
different selected maps.

| Frozen case | Spawn | Authored origin before → proposed origin | Displacement |
| --- | ---: | --- | ---: |
| rooms8-seed17 | 25 | (−456,1056,24) → (−520,1056,24) | 64 |
| rooms8-seed42 | 25 | (−592,288,24) → (−656,288,24) | 64 |
| rooms6-seed7 | 18 | (760,320,24) → (696,256,24) | 90.510 |
| rooms6-seed101 | 20 | (−776,288,24) → (−712,160,24) | 143.108 |
| rooms8-seed101 | 22 | (−544,−1200,24) → (−608,−1200,24) | 64 |

All five first attempts select one move on pass1, each from24 tested eligible
stations. The final objective is(0,0,0) in each case. Their local compiled
standing support passes; no input or generated artifact is written. This is
feasibility evidence for the five warning cases, not the matched default-pipeline
result. The integrated helper runs after the existing visibility refinement for
2–8-room requests, preserving that stage's blocked pairs. The matched default
comparison of16 requests and the separately declared wrapped example belongs
to the Braid release, separately from these five historical feasibility cases.
Candidate identity and unchanged construction require their own attribution;
the earlier wrapped-intent intervention can already change selected maps before
the local spawn pass is enabled.

The wider baseline observation record independently checks84 frozen generated
spawn footprints across all16 Lattice selections. It retains all original
warnings. The six archive maps have44 authored spawns and154 unordered pairs.

## Validation and retained evidence

Fifteen C++ control groups cover implicit-shell occlusion, explicit blockers,
per-pair exposure exchange, zero-problem no-op, same room/floor, outside endpoints,
full hull clearance/support,64-unit anchor boundary and nonblocking lights,
nearest24/no adaptive expansion, deterministic ties, malformed identities and
scope/convex/liquid/mover exclusions, common shell-floor requirements and invalid
explicit intents. Rejected public inputs leave the caller's
canonical layout unchanged. The result field `accepted_moves` counts accepted
updates and can include the same station twice.

The first CTest run exposed a test expectation error: the negative outside-origin
fixture throws the shared analyzer's `VisibilityError`, whereas its catcher
accepted only `std::invalid_argument`. The test now accepts those two specific
input-error types and still checks nonmutation. The helper behavior was unchanged;
the first failure remains in `out/braid-portfolio/spawn-helper-tests.log`.
The initial corrected13 groups and final15 groups pass. Six Python controls protect distance boundaries,
unordered/directed counts, verticality-versus-occlusion, invalid-eye unknowns,
authored eye offset, duplicate identities and nonfinite coordinates.

```powershell
python -B -m tools.map_study.spawn_separation --verify
python -B -m unittest discover -s tests -p test_spawn_separation_study.py -v
build/windows-clang/qmg_spawn_refinement_tests.exe
```

Observation record (`out/spawn-separation-study/observations.json`, written by `python -B -m tools.map_study.spawn_separation`),
first feasibility results (`out/spawn-separation-study/feasibility.json`, upstream study evidence, not imported),
[C++ controls](../tests/test_spawn_refinement.cpp), and
[Python controls](../tests/test_spawn_separation_study.py) retain the exact scope.
No game client, native movement run, user input or capture was used. No scratch
directory was created or removed.
