# What the inherited ranking inputs actually measure

Room-volume variation and closeness to a 1,280-unit room-center spacing are
representation-dependent preferences. They are not universal measures of map
quality. Original controls below change those inputs without changing collision
space, and change a supported crossing without changing the inputs. The existing
`q2rdm2` evidence shows why actual route length and viewpoint-specific visibility
need their own measurements.

The [machine-readable record](../knowledge/q2/ranking-feature-study.json) binds
the sources, original controls, compiled geometry and results. This study adds
no ranker tuning, trained weights, reference geometry to the generator, or new
reference maps. The separate diagnostic implementation exposes the existing
inputs and weights while preserving their formulas and selection behavior.

## Definitions before interpretation

The analyzed ranker source snapshot (`out/ranking-feature-study/ranker-source-snapshot.cpp`, upstream study evidence, not imported)
uses eight clipped inputs. Names below match the diagnostic interface.

| Input | Inherited calculation | What it does not establish |
| --- | --- | --- |
| `connection_count_ratio` | Connection records / max(1, rooms−1), capped at 1 | Collision-valid routes, distinct physical approaches |
| `cycle_rank_ratio` | (connections−rooms+1)/2, clipped to [0,1] | General cycle rank when disconnected, ordinary movement around a cycle |
| `raised_platform_count_ratio` | Raised-platform solid records / max(1, rooms/2), capped at 1 | Distinct upper spaces, useful floor area, access or return |
| `room_volume_cv_scaled` | Twice the population coefficient of variation of room-box volumes, clipped to [0,1] | Perceived room identity, shape, usable area or actual spatial variety |
| `branching_room_count_ratio` | Rooms with at least three incident connection records / 2, capped at 1 | Separate entrances or independent movement choices |
| `spawn_count_ratio` | Spawn-role placements / max(2, target players), capped at 1 | Spawn safety, distribution, selection policy or competitive balance |
| `weapon_count_ratio` | Weapon-role placements / room count, capped at 1 | Weapon usefulness, placement risk, pickup reachability or resource balance |
| `connected_room_center_distance_score` | 1−clamp(abs(mean connected-center XY Manhattan distance−1280)/1280, 0, 1) | Passage length, bends, height, obstacles, time or exposure |

Volume is width×depth×height of each declared room AABB. A volume CV of 0.5
already saturates its input. Center distance uses each connection record, so
parallel records contribute repeatedly. Z and passage-volume dimensions do not
enter it. Its peak at 1,280 matches the numeric grid constant in the retained
generator source; this is a code observation, not evidence for an optimum
spacing in playable maps. The score also treats mean distances 640 and 1,920
equally (0.5), and gives the same maximum to a mixture whose mean is 1,280 as
to uniformly spaced centers. These are direct consequences of the formula.

## One compiled space, four different metadata scores

The original `open-chamber.bsp` has a sealed six-brush shell around the free
box **[0,0,0]–[2048,1024,256]**, with fixed entities. Four analytical room
partitions describe that same box. Every variant has four adjacent convex AABB
regions, three chain connection records (1–2–3–4), four spawns, four weapons,
and no raised-platform records. No partition line creates a wall.

The script verifies containment, disjoint interiors and exact volume coverage
of the same closed union: **536,870,912 cubic map units**. All four variants
refer to the **same BSP bytes**; they are not four separately generated maps.
The complete standing hull can cross (64,512,0)–(1984,512,0), length **1,920**,
with full-footprint floor support in this common world.

| Partition metadata | Strip sizes | Volume CV | Clipped volume input | Mean center XY distance | Center input | Reconstructed score |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| Equal X | 512, 512, 512, 512 | 0 | 0 | 512 | 0.400000 | 3.82 |
| Unequal X | 256, 256, 512, 1024 | 0.612372 | 1 | 469.333333 | 0.366667 | 4.16 |
| CV-only X | 256, 768, 256, 768 | 0.5 | 1 | 512 | 0.400000 | 4.17 |
| Equal Y | 256, 256, 256, 256 | 0 | 0 | 256 | 0.200000 | 3.76 |

All other feature inputs are [1,0,0,*,0,1,1,*]. The MP request uses target
players4, loops enabled, verticality0.5; weights in feature order are
[1.4,1.5,1.1,0.35,0.4,1.2,1.1,0.3]. The table reconstructs F32 feature inputs
and sums their products; it does not claim bit-identical ggml reduction.

The independent actual-ggml `partition_sensitivity` fixture confirms the first
two rows' feature change and the **+0.34 score difference** within 2e−6. The
unequal partition wins that two-candidate metadata test. Its retained
test source (`out/ranking-feature-study/ranker-diagnostics-test-snapshot.cpp`, upstream study evidence, not imported)
and successful execution log (`out/ranking-feature-study/ggml-diagnostics.log`, upstream test log, not imported)
are separate evidence from the compiled hull checks. That fixture uses the
same partition bounds and counts; its entity coordinates differ from this
original MAP control, which does not affect these inputs. The additional two
table rows are analytical controls, not additional actual-ggml test cases.

These controls do not assert that arbitrary partitions pass the production
Layout validator or that canonical MapBuilder emits an unchanged world from
them. They establish a narrower, reproducible fact: equivalent adjacent convex
descriptions of one verified collision space can change these ranker inputs.
The score change alone is therefore insufficient evidence of improved space.

A further exact-union arithmetic control splits one raised-platform box into
two adjacent pieces at X1024. At four rooms, the platform input increases
**0.5→1** while the solid union is unchanged. This control verifies exact box
coverage; it is neither another compiled map nor an elevated movement test.

## One unchanged feature vector, two different crossings

The second original world adds one non-raised structural barrier:
**[896,384,0]–[1152,640,192]**. The shell, room metadata, connection records,
entity positions and counts stay fixed. Because none of the eight inherited
inputs observes this kind of obstruction, all inputs remain equal by their
source definitions.

The resulting seven-brush `barrier-chamber.bsp` blocks the 1,920-unit direct
crossing: the full standing sweep intersects compiled brush3. The same floor
still supports its footprint, so the failure is collision, not missing ground.
A selected northern path through (64,704,0) and (1984,704,0) passes both complete
standing-hull clearance and full-footprint support. Its length is **2,304**,
384 units (20%) longer than the original straight crossing. It also passes in
the open world. The detour is not claimed to be shortest or dynamically tested.

Both controls are sealed QBSP builds with the pinned local compiler. These
small collision controls deliberately have **no VIS or lighting build**. The
geometry checks use model0, player-blocking mask33619971, a 32×32×56 standing
hull, actual stored planes and zero collision epsilon. They establish the
specific static segments, not steering, native movement or match quality.

The actual-ggml diagnostic fixture independently changes all passage-volume
dimensions while holding the inherited metadata inputs fixed and confirms
unchanged features and scores. Those altered passage records are an analytical
input fixture; they are not presented as valid built routes.

## Existing q2rdm2 evidence: same endpoints, different routes

The [interlocking topology study](Q2_INTERLOCKING_TOPOLOGY_STUDY.md) checked two
lower approaches in **The Chastity Belt Duel**, German “JaLisK0” Garcia,
`q2rdm2`, Q2R TMP Edition. Its shared endpoint XY coordinates are A(−160,560)
and B(640,592). Their Manhattan distance is **832**. The selected northern
polyline is **1,312** units (1.577× that distance); the southern polyline is
**1,760** (2.115×). The tempting 832-unit central polyline intersects four
world brushes and lacks complete support.

The south route has strict flat full-footprint support and continuous standing
hull clearance. The north uses a weaker shallow-ground-band contract: ground
continuously lies within [−290.015625,−288] below the feet−288 clearance
envelope. It is not certified as constant-floor or native grounded movement.
Those distinctions remain in the source audit; the routes are not silently
combined as equally verified ordinary traversal.

One independently supported northern observer sees 34/44 sampled north target
positions and 0/60 south positions. This is fixed-view point-ray evidence, not
time exposed, opponent control or proof of a safe bypass. Neither selected
route is claimed to be a globally shortest path.

Applying the center formula to **these endpoints** would yield 0.65 for both
approaches. That is an illustrative information-loss comparison: A and B are
not extracted room centers, and this study does not segment or score the full
reference map. The reliable local lesson is to retain actual supported paths
and their visibility samples when evaluating route alternatives.

## Consequences for design knowledge

Treat declared room dimensions, variation and spacing as typed structural
intent with provenance and an explicit room-definition convention. A request
for different room roles or heights can use these descriptors as construction
targets. It still needs evidence that the resulting spaces are useful.

Keep geometry and traversal checks independently reportable: connected floor
regions, complete supported paths, elevation contracts, alternative approaches
between shared endpoints, and selected viewpoint rays. Bind path metrics to
the actual solids and state the movement contract. Record raw counts and
clipping alongside a score so saturation and duplicate records remain visible.

Before promoting a descriptor to a quality preference, test invariance under
equivalent metadata partitions or solid-piece splits, sensitivity to real
obstructions, and a stated validation population. Human play and native
movement remain separate evidence. This study justifies better diagnostic
semantics; it does not identify replacement weights, an optimum spacing,
universal room segmentation or a learned preference model.

## Reproduction and provenance

From the project root:

```powershell
python -B -m tools.map_study.ranking_features --compile
python -B out/ranking-feature-study/record_evidence.py
```

Omit `--compile` to inspect the retained BSPs without rebuilding. Exact commands
and compiler pin are in `out/ranking-feature-study/compile-record.json`.
The archive BSP remains SHA-256
`d6ee3b39d4e557e7ad5cd39d2bf955e8251edd7e491f2033eee80500791d7199`.
The original readme dates it May2001 with a June update; archive metadata says
August2001. The author specifies duel2 while the archive also lists 2–4-player
FFA/2v2. These unresolved revision/capacity differences are preserved.

Its readme prohibits using the map as a base and restricts redistribution;
training rights remain unreviewed. This study reuses local analysis facts and
pins the original bytes. All new control geometry is project-original. The
reference partition, source rights, model profile and weights are unchanged.
