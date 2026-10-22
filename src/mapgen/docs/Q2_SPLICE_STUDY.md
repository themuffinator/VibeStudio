# Splice: a wrapped perimeter in default generation

The default Quake II deathmatch and duel generator now offers a second room
arrangement: unequal perimeter rooms wrap around a playable central room, with
an outer route that bypasses the center. This changes actual room adjacency,
alongside the existing raised crosswalks, horseshoe galleries and offset
barriers. Open the Splice layout (`out/splice/index.html`, upstream study evidence, not imported), compare the
nine selected maps (`out/splice-portfolio/index.html`, upstream study evidence, not imported), or download the
Splice package (`out/splice/qmg_splice.zip`, upstream study evidence, not imported) and
portfolio package (`out/splice-portfolio/qmg_splice_portfolio.zip`, upstream study evidence, not imported).

The result has a clear limitation: offering two construction families did not
increase the number of selected room graphs in this sample. The selector chose
the new family in every eligible comparison case. Six-room maps gained a
different topology; the eight-room sample went from two distinct selected
graphs to one. This is a construction and measurement experiment, with no
claim that additional cycles, a smaller envelope or a particular graph plays
better. The [experiment manifest](../knowledge/q2/experiments/splice-v1.json)
binds the final sources, artifacts and evidence.

The [builder](../src/mp_motifs.cpp) offers the new family for five through eight
rooms. Odd **candidate seeds** select the pinwheel construction; even candidate
seeds retain rows. The default generator advances candidate seeds by an odd
stride, so its six-candidate batch contains three of each family for these
counts. Other counts retain rows within the existing 2–32-room contract. The
family decision consumes no random draw from the row construction stream.
Explicit family tags change its metadata and canonical identity, while the
retained row geometry policy is unchanged. Request seeds and selected candidate
seeds are different fields and must not be conflated.

Four rectangular perimeter arms turn around the center. For six through eight
rooms, one through three seeded arms split into smaller bays. The resulting
outer rooms form a connected cycle; four separate faces connect the center to
that cycle. Openings remain 48 units long and 160 units wide, with sufficient
shared face overlap. Every bay retains two outer-cycle neighbors. The center
therefore supplies another approach without being the only room-graph route
between the perimeter destinations.

A failed first construction exposed a useful constraint. Splitting a minimum
1,328-unit arm into two 640-unit bays could align its 48-unit seam with a center
seam, recreating a straight gap across the whole plan. The final construction
uses bands at least 688 units wide and excludes cuts whose seam overlaps that
center seam. All resulting room sides remain at least 640 units. The
[topology controls](../tests/test_mp_topology.py) retain a reconstructed version
of the failed arrangement; the [motif tests](../tests/test_mp_motifs.cpp) check
the actual generated rectangles. Absence of a complete empty strip between
room rectangles is a spatial descriptor, not a measurement of playable floor
area, collision-free void or tactical independence.

The existing room motifs retain 24-unit decks, 128-unit-wide stairs, 36-unit
treads and 16-unit rises. Each elevated room has two ordinary stair accesses;
the source tests remove each complete run in turn and retain required ordinary
routes. Crosswalks also preserve a continuous standing-hull lower passage.
Low verticality omits raised platforms. The candidate tests exercise both
128- and 192-unit decks where the actual room dimensions permit them. The
selected comparison maps and Splice use local floors 0/128, not two complete
copies of the map above each other. Allocation and bounded spawn refinement
retain the preceding [Weft policy](Q2_WEFT_STUDY.md).

The fixed case list (`out/splice-portfolio/cases.json`, written by `python -B -m tools.mp_portfolio.topology_compare`) contains four, six and
eight rooms crossed with request seeds 17, 42 and 99. Four-room cases request
two-player duel; the others request six-player deathmatch. Their copied,
compiled baselines precede this topology change. A separate
hero declaration (`out/splice-portfolio/hero.json`, written by `python -B -m tools.mp_portfolio.topology_compare`) fixes **six rooms, seed 73**
before editing the core. Its display title, Splice, does not substitute a
candidate or change the emitted MAP/report.

The [candidate observer](../tools/mp_portfolio/trace_candidates.cpp) makes the
real six-candidate generator/ranker calls and independently checks the ordinary
pipeline's selected identity and rank score. Retained traces describe candidates
before repair; final exports remain the default pipeline selection. The
comparison (`out/splice-portfolio/comparison.json`, written by `python -B -m tools.mp_portfolio.topology_compare`) checks current artifact
hashes and binds each trace, including the hero, to the final canonical layout.
The ranker's existing heuristic and its 1,280-unit center-distance preference
remain unchanged. The sampled preference for the new family is an observation,
not a newly trained preference or a guarantee for other prompts.

| Request | Compiled XY envelope change | Passages before → after | Maximum room hops before → after |
| --- | ---: | ---: | ---: |
| 4 rooms / seeds 17, 42, 99 | 0.0% each | 5 → 5 | 2 → 2 |
| 6 rooms / seed 17 | +10.2% | 7 → 9 | 3 → 2 |
| 6 rooms / seed 42 | +12.7% | 7 → 9 | 3 → 2 |
| 6 rooms / seed 99 | +17.7% | 7 → 9 | 3 → 2 |
| 8 rooms / seed 17 | −24.4% | 12 → 11 | 3 → 3 |
| 8 rooms / seed 42 | −25.1% | 13 → 11 | 3 → 3 |
| 8 rooms / seed 99 | −18.3% | 13 → 11 | 3 → 3 |
| Separate Splice / 6 rooms / seed 73 | +8.5% | 7 → 9 | 3 → 2 |

Envelope area uses the bounding rectangle of compiled model-0 brushes,
including the shell. Six-room growth of 10.2–17.7% accompanies the new route
arrangement; the eight-room reductions are 18.3–25.1%. These matched requests
change geometry and placements together and do not isolate a gameplay benefit.
Room hops count unweighted adjacency edges, ignoring internal turns, elevation,
travel distance, opponents and movement time.

The canonical room multigraph ignores room names, IDs, coordinates, rotations
and ordering. In the three cases at each room count, distinct selected graphs
are **1 → 1 for four rooms, 1 → 1 for six rooms, and 2 → 1 for eight rooms**.
Each six-room candidate batch offers two graph signatures; each eight-room
batch offers three. Offered variety and selected variety are separate results.
The new family changes all six eligible selected graphs relative to their
baselines, but does not establish greater delivered tactical variety.

Splice selects candidate seed `4354685564936845427`, index 2, without repairs;
its trace (`out/splice/candidate-trace.json`, written by `python -B -m tools.mp_portfolio.topology_compare`) retains all six scores. Six rooms
contain nine passages, eight stair runs, four elevated rooms and two offset
barriers. The center is Gallery 1, and the perimeter includes another gallery,
two crossings and two bays. Six spawns, six weapons, six health placements and
four armor placements make 22 gameplay anchors and 132 required ordinary pairs.
Its compiled XY envelope grows from 4,792,320 to 5,198,336 square units. Authored
room/passage union coverage is separately 95.0% of its 2,224×2,208 envelope,
before subtracting internal solids. Neither figure is net usable floor area.

All nine final comparison BSPs and the separate Splice BSP pass sealed QBSP,
full VIS and extra4 one-bounce lighting. Their local compiled standing-hull,
support, node-center and tread checks pass. All 960 ordinary pairs across the
nine primary maps, and Splice's 132 pairs, remain reachable in the serialized
walk/step/door graph. Removing each passage's navigation nodes loses no required
pair. Source construction tests also remove each actual connection in turn.
These graph interventions and local compiled checks do not execute every
continuous journey natively or simulate an opponent blocking a doorway.

Every final map has zero visible spawn pairs under the compiled world eye-ray
measurement, with no source navigation, visibility or quality-owned findings.
**Thirty-four analytical lighting warnings remain across the nine primary
maps; Splice retains four connection-contrast warnings.** The hero's baseline
also had two spawn-separation warnings, which are absent in the new output,
but several design variables changed. These results do not prove balanced
spawn selection or rendered readability.

The six focused regression gates (`out/splice-portfolio/focused-tests.log`, upstream test log, not imported),
separate motif gate (`out/splice-portfolio/motif-tests.log`, upstream test log, not imported) and
final analysis gates (`out/splice-portfolio/final-analysis-tests.log`, upstream test log, not imported) pass:
eight distinct affected CTest groups, with topology checked again after its
final stale-trace and slicing-gap controls. This is a focused iteration run;
the preceding Weft delivery retains its historical full-suite result. The motif
gate includes 48 family candidates across five through eight rooms and low/high
verticality, physical layout checks, independent upper accesses and ordinary
connectivity after individual passage removal. The
preview records (`out/splice/preview-checks.json`, upstream study evidence, not imported) cover 180 states across the
nine maps and Splice: three widths, both themes and stacked/0/128 views.
The diagrams preserve common XY scale and origin between floor plans. They are
analytical views of geometry, not engine renders.

Ordinary CLI parity (`out/splice-portfolio/cli-parity/parity.json`, upstream study evidence, not imported) reproduces
the delivered Splice MAP and report byte-for-byte. The three four-room MAPs also
match their copied baselines exactly; their
layout/report controls (`out/splice-portfolio/cli-parity/four-room-semantic-parity.json`, upstream study evidence, not imported)
differ only in the family tags and canonical hash labels. All
eight published recipe MAPs (`out/splice-portfolio/recipe-parity/parity.json`, upstream study evidence, not imported)
remain byte-identical after rebuilding against this core. Historical recipe
artifacts were not overwritten.

The dedicated smoke check (`out/splice/engine-smoke/engine-smoke.json`, upstream study evidence, not imported) passes
all 16 checks on the final lit BSP, initializes its native entities and completes
40 scheduled post-spawn server frames without matched initialization failures.
The child exits normally, and the exact runtime executable has no remaining
live process (`out/splice/engine-smoke/process-closure.json`, upstream study evidence, not imported). No client,
connected player, input device or render capture participates. The MAP SHA-256
is `fad5d52d51ddbb1f71742c9b9d1a41351c4545a02c24fbfab738de3f7559e4cb`;
the tested BSP is
`3e6a8aa5c6170df5d07d27f16cc13daacaa6ad080e711e3501264b0cb17dd504`.

Two parallel studies sharpen what to test next. The
[endpoint-paired reference study](Q2_INTERLOCKING_TOPOLOGY_STUDY.md) examines
opposite approaches to the same rocket station in The Chastity Belt Duel.
Its longer southern approach is outside one fixed northern viewpoint's sampled
visibility. The northern corridor needs a separate shallow ground-band
certificate; that geometric evidence is not a native movement proof. This
supports measuring destination, exposure and actual approach separately from
whole-map graph centrality, without copying the reference geometry.

The [native motif circuit study](Q2_NATIVE_MOTIF_CIRCUITS.md) concerns the frozen
**Weft BSP, not Splice**. All eight first fixed-input attempts fail their strict
complete-route protocol, including its narrow upper-exit gate. Later observations
find that four of those same traces stay in the actual full-hull stair band,
with supported upper-plane chords through the exit; these are observations of
existing attempts, not four new control experiments or promoted successes.
The other four show partial edge support. The results neither establish that
the motifs are untraversable nor certify Splice movement.

The next useful comparisons pair routes between the same resource destinations,
measure exposure from declared viewpoints, and exercise complete native turns,
stair exits and stopping space before multiplayer trials. More candidate
families alone will not solve a selector that repeatedly prefers one graph.
No model weights or reference-profile bands were trained or changed in this
iteration; the current results remain original procedural blockouts with
construction evidence and explicit movement and gameplay work still to do.
