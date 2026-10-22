# First Quake II study — 10 September 2026

The archive now supplies a measured local reference base, and the current generator has a reproducible comparison baseline. The most useful immediate experiments concern **usable combat elevation, respawn alternatives, major-resource contests and capacity-aware spacing**. Cosmetic texture variety is a later pass; a texture count is not a quality target.

The reusable [regimen](MAP_STUDY.md), [curriculum](../knowledge/q2/curriculum.json), [rule cards](../knowledge/q2/rules.json), and [machine-readable first observations](../knowledge/q2/first-study.json) are tracked in the repository. Map-derived dossiers, geometry diagrams and full evidence remain local in `out/map-study/`. The ten generated maps and their compiler evidence are in `out/map-study-baseline-final/`. The earlier `out/map-study-baseline/` directory is retained as a superseded run before pickup-compatibility admission; use the final directory for current comparisons.

## What was actually run

- Parsed and analyzed all **460 IBSP38 maps**, with no quarantined BSPs or followed filesystem links in the completed strict run.
- Inventoried **454 AAS**, **429 entity sidecars**, and **19 `.map` files**. All entity sidecars match their BSP's parsed embedded entity content. Source-file authorship is still unverified.
- Created a **26-map, 12-lesson** study syllabus and **15 design-rule hypotheses**.
- Grouped the corpus into **127 author/family components** before splitting: **299 reference**, **117 validation**, **37 test**, **7 study-only maps**. These are provisional lineage splits, not a completed leakage audit.
- Generated and compiled all **10/10 fixed benchmark cases** with the pinned Q2 compiler. Eight received numeric reference comparisons. The two CTF cases were measured but deliberately received no numeric comparison because their cohort is too small.
- Passed the full **17/17 CTest suite**, including the new Python study suite, Q1/Q2/Q3 compiler/leak/VIS/light gates, grammar checks and the local GGUF planner test. The Python suite was then expanded to **16 passing tests**, including corrupt envelopes, thin-wall occlusion, directed routes, split integrity, deterministic output, unknown-light handling and unsupported-pickup admission.

This was a static/compiler study. No engine match, navigation playthrough, human preference test or neural-weight training is represented by these results.

## Corpus measurements that can guide experiments

Each independent author/family component contributes one median. The bands below are p10–p90 across those component medians, not limits every good map must satisfy.

| Cohort | Maps | Independent groups | Combat-height spread band | Median number of DM spawns | Median major-item count |
| --- | ---: | ---: | ---: | ---: | ---: |
| Duel | 130 | 50 | 222–453 units | 7 | 2 |
| Small DM, archive maximum ≤6 | 112 | 47 | 233–475 units | 7 | 2 |
| Medium DM, archive maximum 7–12 | 142 | 63 | 248–545 units | 8 | 2.5 |
| Large DM, archive maximum >12 | 13 | 10 | 339–885 units | See local profile | See local profile |
| CTF | 3 | 3 | Insufficient groups for comparison | — | — |

Maps can belong to both duel and DM cohorts. Capacity labels come from the archive, not newly measured load tests. Fractional counts in group statistics arise from medians/interpolation across maps and groups; they do not describe fractional entities in an actual map.

“Combat-height spread” is p90−p10 of authored spawn, weapon, armor, major-item and objective origin heights. It is not ceiling height or navigable height. “Major items” here means quad, invulnerability, megahealth, body armor, power shield or power screen; ordinary armor/health and powerful weapons remain separate categories.

The maps offer useful contrasts rather than a single template. The Rage has a 302-unit combat-height spread, Aerowalk 352 and Powertrip 330. Their tested spawn-eye pairs have no static world LOS. The Triple Towers has a lower 206-unit spread and 20% spawn-pair LOS; Paranoia has approximately 17%. Those differences are reasons to inspect context and counterplay, not to label the latter maps failures. Japanese Gardens has an 848-unit spread and a larger archive capacity, reinforcing the need to compare intended scale and mode.

## Generator findings and the next controlled changes

| Finding in this baseline | Reference evidence | Proposed experiment |
| --- | --- | --- |
| High-verticality duel: **96–112 units** of combat-height spread, one coarse 128-unit band | Duel component-median band **222–453** | Build connected upper/lower combat routes and put useful approaches/resources on them. Preserve hull and directed-route validation. Compare actual use of each level. |
| Duel: **2 spawn positions** at two-player capacity | Reference median **7**, band approximately **5–9** | Decouple respawn alternatives from concurrent player count. Compare validated spawn sets, recovery routes and immediate deaths. |
| Duel and FFA cases: **0 major items in the defined category** | Duel median **2**, band **1–3** | Introduce appropriate contested resources and compare concentrated/distributed circuits with the same timers and counts. Existing ordinary armor/health is not absent. |
| Four-player FFA world diagonal: **5,137–5,161 units** | Small-DM band **2,365–3,845** | Test shorter travel between differentiated spaces. Measure encounter intervals and empty travel; world bounds alone cannot prove oversizing. |
| Duel median sampled sightlines: **293–350 units** | Duel band **141–226** | Add selected line breaks and alternative approaches. Preserve some long-range opportunities and compare engagement distances/weapon choices. |
| Duel material entropy: **1.47–1.50 bits** | Duel band **2.68–4.24** | After spatial changes, compare a coherent material/landmark pass. Do not optimize entropy or add arbitrary texture noise. |

The low-verticality gothic cases correctly have only an 8-unit combat-height spread. That is a requested specialization, not itself a failure. Preparing this comparison uncovered an existing prompt parser bug: the generic `vertical` substring previously intercepted `low verticality`. The parser now gives the explicit low request precedence, and regression tests cover both low and generic height-variation prompts. The recorded benchmark used the corrected binary.

No learned threshold or quality score has been pushed into generation. These observations are tied to exact artifacts in `knowledge/q2/first-study.json`, while the rule cards remain hypotheses awaiting controlled experiments and playtest evidence.

## Evidence problems discovered

1. **All 454 AAS sidecars contain zero reachability records.** Their presence cannot establish usable navigation. Retain their hashes, but obtain correctly generated, BSP-associated navigation data under a known physics/compiler profile or collect engine traversal evidence before drawing route conclusions. Empty reachability is not a verdict that the maps themselves are disconnected.
2. **426 records have metadata issues**, commonly an unterminated `score_level` line. The importer records these and preserves subsequent complete fields. It does not turn that score into a quality label.
3. **Seven maps have unresolved authors:** `base64`, `city64`, `sewer64`, `fedm1`, `hardange`, `match1`, and `rvndm1`. They remain study-only.
4. **`nandb_01` and `nandb_02` have no lighting lump and retain zero sample offsets.** The importer records light evidence as unavailable and still permits geometry/entity study; it makes no rendering/readability claim.
5. **`johnnydm4` and `spogctf1` repeat entity keys.** Native last-value semantics are retained with explicit issues, rather than unnecessarily excluding these maps.
6. **CTF coverage is sparse:** only three independent reference groups. Both CTF baseline cases pass generation/compilation, but reference-band comparison remains unavailable at the configured five-group minimum.
7. **No map has reviewed training admission yet.** Local readmes and source-file presence do not establish all required origin, modification, redistribution and ML terms. The current work supplies local measurements and original design hypotheses, not redistributed map data or a trained model.
8. **19 maps contain unsupported pickup classes**, including alternate-weapons pickups, `ammo_traps`, `item_haste`, `item_teleporter`, `weapon_rockets` and the misspelling `weapon_grenadelaucnher`. They stay visible in the inventory but are excluded from reference calibration until game-module/entity semantics are resolved. Six were in the reference partition; their exclusion leaves the author/family split intact while removing incompatible item economies from the cohort profiles.

No other unrelated repository defect was discovered during this task. The prompt precedence defect above was fixed as part of making the variation benchmark meaningful.

## Reproduction identities

| Artifact | SHA-256 |
| --- | --- |
| Corpus manifest | `434ed06e37447872df92f0eb9a35c7a0d40b59c5cae77709493d544801d87c02` |
| Reference profile | `4f833d1a09eab67c719175310e101befcc9ad43d330985e7bc7a7d645f65d935` |
| Prompt/seed matrix | `0ad81ca8edbe24dae60dcb03a2197c8c26922237ff851019f2345606d743ef52` |
| Tested generator binary | `8eea67aa3510326cde23c52fe8adb07f81c9f27cc0fce0aaf28798896062aad8` |
| Tested qbsp | `fad5a4dc4daf42773ad9a812349b2667221bec47a920cad10cb821ae442c8eb8` |

The manifest records every source BSP/sidecar hash, relative path, attribution and partition. The benchmark records exact commands and generated source/BSP/portal identities. Hashes bind these observations to this run; they do not authenticate authorship, rights, compiler settings of old sidecars or human playtest results.
