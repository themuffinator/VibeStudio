# Lattice: stop treating unsupported preferences as quality

Lattice is an ordinary default selection from the existing compact Q2
multiplayer generator. This iteration changes how a fixed candidate pool is
ranked. It does not add another architecture, replace candidates, reroll the
example or train a preference model.

Open Lattice's analytical viewer (`out/lattice/index.html`, upstream study evidence, not imported) or the
15-map comparison portfolio (`out/lattice-portfolio/index.html`, upstream study evidence, not imported). Download
the Lattice package (`out/lattice/qmg_lattice.zip`, upstream study evidence, not imported) and the separate
primary/holdout portfolio package (`out/lattice-portfolio/qmg_lattice_portfolio.zip`, upstream study evidence, not imported).
The [experiment record](../knowledge/q2/experiments/lattice-v1.json) binds the
released sources, comparisons and verification artifacts.

The [ranking-feature study](Q2_RANKING_FEATURE_STUDY.md) establishes the reason
for restraint: changing adjacent room partitions can increase the inherited
score while collision space stays identical, and a real barrier can change a
checked crossing without changing those inputs. These controls identify weak
feature semantics. They do not prove that a replacement policy produces better
matches. The preceding [Splice study](Q2_SPLICE_STUDY.md) supplies the separate
procedural observation: richer offered room graphs did not necessarily produce
more varied selected graphs.

## What changes in default selection

For matching **Q2 deathmatch/duel requests with 2–8 rooms**, when every candidate
has the requested room count and every room carries the existing `compact_mp`
tag, the ranker sets two inherited weights to zero:

- `room_volume_cv_scaled`, which describes declared room-box volume variation.
- `connected_room_center_distance_score`, which favors mean connected-center
  XY Manhattan spacing near 1,280 units.

Both feature values remain visible in diagnostics. The six other inherited
weights retain their request-dependent values and ggml still computes the
scores. They are count-based adequacy heuristics, not independently verified
quality gates. Geometric construction, candidate generation, entity allocation,
structural repair and static analyses retain their separate responsibilities.

Exact best-score ties use the minimum SplitMix64 permutation of candidate seed,
then the rank-neutral canonical layout hash. The rule is deterministic and
independent of candidate order or family labels. It is not a learned preference,
a family quota, a ranking of gameplay value or a guarantee of diversity. Unique
64-bit seeds have unique permutation keys; the hash fallback handles equal
seed keys. Outside the matching marked 2–8-room Q2 multiplayer scope, the legacy
weights and first-maximum tie behavior remain.

The full request comparison covers source prompt, title, theme, game, mode,
seed, room count, target players, verticality, intensity and loop preference.
Mixed or mismatched candidate specs do not silently opt into the policy.

## Fixed requests and preserved candidate evidence

The primary set retains the nine earlier requests: 4, 6 and 8 rooms at seeds
17, 42 and 99, with the normal six candidates. Six separately declared holdouts
use 6 and 8 rooms at seeds 7, 101 and 307. All holdout seeds are odd; this small
set does not establish population-wide behavior. The separately predeclared
example is **8 rooms, seed211**, displayed as **Lattice**. Its title changes
only the analytical viewer label.

Before changing selection, additive diagnostics retained all eight feature
inputs, request weights, products and authoritative ggml scores for all
**96 candidate records across 16 requests**. The old CLI outputs and diagnostic
exports agree in **32 MAP/report byte comparisons**. Five requests outside the
new scope retain **10 legacy CLI MAP/report byte comparisons**. These are file
comparisons, not 32 or 10 different maps.

Within every recorded baseline pool, only the volume-CV and center-distance
inputs vary. Removing their contributions is therefore an explicit decision
to stop distinguishing these candidates on those grounds; it does not reveal
a latent ordering of better maps. Other request populations can vary the
remaining features and need their own checks.

The final comparison binds each request to its frozen trace SHA and retained
baseline/diagnostic MAP/report pairs. For every corresponding candidate it
requires unchanged rank-neutral canonical layout hash, feature inputs, family,
room IDs and connection records. It requires zero repair passes in both runs.
The comparison therefore isolates selection over the retained candidate pool;
different selected maps can still differ in geometry and placement.

The checker verifies exact semantic feature order, finite eight-element input,
weight and product vectors, products within 1e−6 and authoritative score sums
within 2e−6. It checks maximum-score membership, the declared tie set, seed keys
and pipeline selection identity. Thirteen controls include forged scores,
products, reordered feature names, stale pool hashes and changed parity bytes.
Only final selected outputs receive compiled route/placement checks; these
checks do not certify every offered candidate.

## Measured selection and compiled results

The final comparison (`out/lattice-portfolio/comparison.json`, written by `python -B -m tools.mp_portfolio.ranking_compare`) confirms all
**96 corresponding candidate records** retain their rank-neutral layout hashes
and complete feature vectors. Every final pool has six exactly tied best scores.
The selected seed changes for 14 of the 16 requests. Canonical room-multigraph
counts below ignore names, coordinates, ordering and rotations, but retain
parallel passage records. They describe the three selected maps in each row.

| Sample / room count | Requests | Distinct selected graphs before → after | Final families |
| --- | ---: | ---: | --- |
| Primary / 4 | 3 | 1 → 1 | 3 rows |
| Primary / 6 | 3 | 1 → 2 | 2 rows, 1 pinwheel |
| Primary / 8 | 3 | 1 → 2 | 2 rows, 1 pinwheel |
| Holdout / 6 | 3 | 1 → 2 | 2 rows, 1 pinwheel |
| Holdout / 8 | 3 | 1 → 3 | 2 rows, 1 pinwheel |

This is increased delivered graph variety in the declared samples, not proof
of better flow. Compiled envelopes move in both directions. Primary changes
span **−14.49% to +37.93%**: the eight-room rows selections at seeds42/99 grow
30.52%/37.93%. Holdout changes span **−18.28% to +23.28%**; eight-room rows at
seeds7/101 grow18.96%/23.28%. No smaller-envelope or lower-room-hop objective
was substituted to select a more flattering example.

All sixteen selected outputs compile sealed with **full VIS and extra4
one-bounce lighting**, and pass local compiled node, tread and anchor
hull/support checks. Ordinary directed spawn-to-anchor graph pairs total
**960 primary, 882 holdout and 162 example**. None is missing; no individual
passage-node-set removal loses a required pair. All final compiled spawn-eye
pair line-of-sight fractions are zero. These successes coexist with warnings:

| Analyzer findings, before → after | Nine primary | Six holdout | Lattice example |
| --- | ---: | ---: | ---: |
| Navigation | 0 → 0 | 0 → 0 | 0 → 0 |
| Visibility | 0 → 4 | 4 → 6 | 2 → 0 |
| Lighting | 34 → 41 | 20 → 28 | 3 → 5 |
| Quality-owned | 0 → 0 | 0 → 0 | 0 → 0 |

Every final visibility finding is `insufficient_separation`. Each affected
spawn pair produces two directed findings; this is separate from its blocked
eye ray. New separation warnings appear in primary rooms8-seed17/42 and
holdout rooms6-seed7/rooms8-seed101. Holdout rooms6-seed101 already warned but
changes the close pair, while rooms8-seed7 and the example lose their old
separation findings. The primary and holdout regressions remain in the
delivered evidence. Zero quality-owned findings cannot be described as zero
overall findings or complete spawn safety.

The predeclared Lattice example selects the existing **rows** family: eight
rooms, ten passages, three raised crosswalks, two horseshoe galleries and three
barrier rooms. Ten stair runs contain 80 treads, connecting local floors0/128.
Six spawn positions and 27 gameplay anchors produce 162 ordinary directed
spawn-to-anchor graph pairs, with none missing. Compiled local node, tread and
anchor hull/support checks pass. No individual passage-node-set removal loses
a required pair. Those graph interventions do not physically compile doorways
shut or certify complete native journeys.

| Matched seed211 selection | Previous pinwheel | Lattice rows |
| --- | ---: | ---: |
| Rooms / passages | 8 / 11 | 8 / 10 |
| Maximum unweighted room hops | 3 | 4 |
| Mean unweighted room hops | 1.785714 | 2 |
| Compiled model0 XY envelope area | 5,343,744 | 5,776,128 |
| Gross authored room/passage envelope coverage | 93.995% | 94.243% |
| Analytical lighting warnings | 3 | 5 |
| Visibility-owned insufficient-separation warnings | 2 | 0 |

Lattice's compiled envelope is **8.0914% larger**. Full-width room-only slicing
gaps return at X[856,888] and Y[24,72]. It is therefore not a new non-slicing
arrangement or demonstrated improvement in tightness or room-route length.
Gross authored box-union coverage excludes internal barriers and does not
measure walkable density. Unweighted room hops are not travel time.

The final hero has no navigation, visibility or quality-owned findings and no
compiled visible spawn-eye pairs. **Five lighting warnings remain**: one
unreadable sample and four excessive connection-contrast findings. Removing
the two old separation warnings does not establish balanced spawn placement.
The allocator still follows its existing requested-player spawn-count policy.

The next construction experiment should make requested spatial intent explicit:
for example, a wrapped arrangement with a supported outer bypass, useful local
upper routes and defined spawn-separation constraints. Such a contract can be
checked against actual geometry without declaring one room graph universally
better. Changes to candidate construction or placement need separate attribution
from this fixed-pool selection experiment, including any tradeoff against line
of sight, space or ordinary route length.

## Native movement remains a separate experiment

The [new turning experiment](Q2_NATIVE_MOTIF_TURNING.md) uses frozen **Weft**
geometry. It does not run Lattice. All **16 new first attempts** complete the
declared physical circuit and final dwell: two motifs, both directions, two
fixed input amplitudes, and rolling versus 24-frame corner releases. The
timings were derived from earlier evidence before execution. No geometry was
changed and no result-triggered rerun was used.

The historical strict protocol remains **0/8**. A separate physical-contract
interpretation of the same old frames passes **2/8**, without adding native
runs. The new 16/16 cannot be compared directly with the historical 0/8 as a
timing-only improvement because the acceptance contract changed as well.

Within the eight matched new pairs, pauses reduce final lateral offsets but
add **688–752ms**; both policies pass. Every final state has zero velocity and
a **0.125-unit hull gap to a room wall**. These are wall-adjacent recovery
results, not open-space stopping precision. Upper geometry proofs concern
reconstructed feet128 chords, not the native engine's internal substeps.
Five broad one-axis station diagnostics still fail alongside the successful
bounded pocket outcomes; eleven controls protect the physical interpretation.
World-only movement remains separate from multiplayer play, item use,
opponents and engine-rendered readability.

## Reproduce and inspect

The unchanged exporter follows ordinary `GenerationPipeline` selection and
retains MAP/report bytes. The candidate tracer independently repeats generation
and ranking and checks its selected identity against the ordinary pipeline.
The [portfolio tooling](../tools/mp_portfolio/README.md) describes these
contracts. The comparison command is:

```powershell
python -B -m tools.mp_portfolio.ranking_compare --output out/lattice-portfolio
```

All **47 configured CTest groups** pass. The subsequently revised ranking and
native-turning groups also pass their targeted rerun, covering 13 ranking
comparison controls and 11 physical-contract controls. Lattice matches the
ordinary CLI's MAP/report bytes and passes **16 dedicated initialization
checks**. Those server checks contain no connected-player journey or rendering
proof. Analytical viewers pass **288 states** across three widths, both themes
and all floor choices: 270 for the 15 comparison maps and 18 for Lattice.

| Lattice artifact | SHA-256 |
| --- | --- |
| MAP | `e5ca81928e9c5a44a67d7732cc6fc57715bfcdb966745f69aad168676c243312` |
| Lit BSP | `e388d6b42c2cd52da88c99f4b049c87a28f9fe69c3f4a35893bdc6bd50d113ea` |
| Hero package | `a4decef5a37501de9a1ce8ba0880a0f00fcf7158a052f1c441399c642806e25c` |
| Comparison portfolio package | `45595cc07c8e6869ed7ca4639e34ad15a8c1b0f17461b2a2c4874558088d48f0` |

The local reference archive,
reference partitions, rights decisions and learned model profile remain
unchanged. This scoped abstention is an explicit hand-authored policy revision;
it is not learned quality evidence or a completed map playtest.

The exact cleanup attempt for `.agents/tmp/lattice-viewer-v1` and
`.agents/tmp/lattice-engine-v1` was rejected by automatic approval review.
Both disposable test directories remain, and no retry was attempted. The
cleanup record (`out/lattice/cleanup-status.json`, upstream study evidence, not imported) preserves the rejection.
