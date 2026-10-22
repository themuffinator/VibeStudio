# Reference-map study regimen

This is the first executable stage of M4: load the user's curated Quake II archive, measure what is actually present, compare generated blockouts with relevant references, and collect evidence for design rules. It produces a local knowledge base and a repeatable testing regimen. It does not claim that a statistical resemblance makes a map good, that the references are all equally good, or that neural weights have been trained.

The initial curriculum selects **26 maps in 12 lessons**. It starts with Quake II multiplayer because that is what this archive predominantly supplies. Quake I, Quake III, single-player encounters, progression, secrets and difficulty curves need additional cohorts and title-specific experiments. Do not transfer Q2 physics or duel item economies to those tasks without validation.

The [first executed study](Q2_FIRST_STUDY.md) records the 460-map audit, ten generated baseline cases, measured differences, evidence gaps and reproduction hashes. Its [machine-readable observations](../knowledge/q2/first-study.json) connect measured findings to the rule cards and proposed experiments.

## Run the study

Python 3.10+ and its standard library are sufficient. Run from the project root. The archive stays read-only; generated files belong under `out/` and remain ignored by Git.

```powershell
python -B -m tools.map_study ingest `
  --archive E:/Development/q2maparchive-wip/maps `
  --output out/map-study `
  --strict
```

Open `out/map-study/index.html`. Its searchable inventory links to every evidence record and to interactive plan views for the curriculum maps. The height selector separates upward surfaces into 128-unit bands; markers identify entity IDs in the evidence JSON. These are analytical diagrams, not engine screenshots or navigability proofs.

Outputs:

| Artifact | Use |
| --- | --- |
| `manifest.json` | BSP/geometry/sidecar hashes, attribution, paths, rights status, issues and author/family partitions |
| `records/*.json` | Measured entities, item economy, combat elevation, surface/material distribution, sampled sightlines, availability and limitations |
| `maps/*.html`, `index.html` | Local study atlas and selected map dossiers |
| `profile.json` | Cohort-specific distributions of author/family-group medians, using only the reference partition |
| `summary.json` | Corpus counts, metadata/sidecar problems and exclusions |
| `study-plan.template.json` | Hash-bound session templates with questions, engine load names, evidence fields and unfilled ratings |

Re-running ingestion regenerates these outputs. **Copy the session template into a separate review directory before filling it in**, for example `out/map-study-reviews/cycle-01.json`. Keep completed human observations outside the generated atlas directory. No rating is inferred from an archive's inclusion, filename, metadata score field or missing observation.

`--strict` returns failure for quarantined BSPs or excluded filesystem links. A missing curriculum map or a contradictory author/family split fails the run rather than silently changing the syllabus. Ordinary metadata issues remain visible in the records. Packed archives are inventoried, not extracted or executed.

## What the extractor knows

The importer checks actual `IBSP` version 38 bytes, lump ranges/overlaps, used record layouts, references, finite numbers and bounded acyclic BSP trees. It parses the embedded entity text with native last-value semantics for repeated properties, recording those repetitions. The `.ref` parser separately salvages complete individual metadata lines and records malformed lines. Metadata salvage never substitutes for entity parsing.

Entity sidecars are hashed and compared with embedded entities; they never silently replace the BSP's gameplay. Present `.map` files are inventoried as **unverified source**, since an extension alone cannot distinguish author source from a decompilation. Archive files, textures and decompiled geometry are not incorporated into the runtime source tree.

Unknown weapon/ammo/item classes are retained as declared entities but make a map ineligible for reference-profile calibration until its game-module semantics are reviewed. This catches both mod-specific pickups and misspellings. Ordinary Quake II pickups and the two supported CTF flags have explicit contracts; a candidate with unresolved pickup classes is also rejected by the comparison command.

| Topic | Available now | Still requires further evidence |
| --- | --- | --- |
| Layout/spaces | World bounds, upward surface area, plan views, gameplay-anchor distributions | Room semantics, usable floor area, spatial rhythm, useful route alternatives |
| Verticality | Combat-anchor height spread and coarse height bands | Hull-valid upper/lower routes, reasons to use each level, advantage/counterplay |
| Sightlines | World-brush point traces between spawn eyes; up to 32 area-weighted face samples, 8 horizontal rays each, 2,048-unit cap | Player facing, dynamic doors/movers, cover effectiveness, actual engagement distances |
| Items/spawns | Stock DM spawnflag filtering, class counts, authored origins, direct distances, major-item separation | Runtime floor drops, mod/dmflag behavior, timed routes, control circuits, spawn deaths |
| Navigation | Bounded EAAS4/5 parsing, directed graph diagnostics and exact point-to-area bindings when data exists | BSP association, compiler settings, correct physics, human/bot traversal and mover states |
| Theme | Archive labels, sky names in worldspawn, surface-area material palette/entropy | Architectural coherence, landmark recognition, texture appearance and sound design |
| Light/performance | Presence of compiled visibility/light data and geometry counts | Light readability, worst-view rendering, frame-time distribution and load behavior |
| Enjoyment/balance | Structured questions and unfilled review templates | Repeated play, blinded preferences, capacity-specific outcomes and counterexamples |

Point sightlines treat solid/window/lava/slime world brushes as blockers and omit entity submodels. They are neither PVS membership tests nor player-hull traces. Authored spawn origins are used before runtime adjustment. Upward surfaces can include inaccessible trim. Direct item distance is not travel distance or travel time. AAS areas are compiler subdivisions, not rooms; special-jump and team-restricted links are excluded from the ordinary-route diagnostic.

These distinctions matter: optimizing a convenient proxy can make actual gameplay worse. A long view can be a purposeful rail lane; a short dead end can hold a worthwhile commitment reward; many texture names can belong to one coherent material kit.

## The bespoke curriculum

The machine-readable syllabus is [curriculum.json](../knowledge/q2/curriculum.json). Its selections are invitations to investigate particular questions, not assertions that those maps have already passed those questions in a new playtest.

| Lesson | Maps | Study focus |
| --- | --- | --- |
| 1 | The Rage (`ztn2dm3`), Powertrip (`ptrip`) | Recognizable spaces, first equipment, return routes and meaningful choices |
| 2 | Aerowalk (`aeroq2`), The Triple Towers (`mktriple`) | Upper/lower combat, return costs, drops, teleporters and counterplay |
| 3 | The Killing Machine (`ztn2dm2`), The Chastity Belt Duel (`q2rdm2`) | Armor/health/weapon circuits, commitment and control |
| 4 | Reclamation (`q2rdm7`), Bio Rust (`koldduel1`) | Combat ranges, ceilings, thresholds, open areas and transitions |
| 5 | Paranoia (`grdm1`), Colours of War (`colour`), Japanese Gardens (`japang`) | Material/architectural identity, landmarks, lighting and sound |
| 6 | One Man's Junk (`bugdm3`), The Hammer (`hammer`) | FFA capacity, encounter rhythm, congestion and recovery |
| 7 | Small Pile of Captures (`spogctf1`), Neutral Grounds (`rqmctf1`) | CTF bases, equipment access, carrier routes and side-swapped fairness |
| 8 | The Arms of Lilith (`spirit2dm2`), Brushed Metal (`poodm6`) | Construction, collision silhouettes, stairs, trim and readable detail |
| 9 | Blood Run (`bloodrun`), `ztn2dm5` | Authorship/lineage, tactical variety and presentation changes |
| 10 | `ikdm1`, `ikdm5`, Heavy Metal (`q2rdm10`) | Reserved validation authors for rubric development |
| 11 | Nineball (`tsh2tourney1`), Holy Grounds (`ohmdm1`), The Grit (`thegrit`) | Reserved test authors for a prospective frozen evaluation |
| 12 | Courts at War (`city64`) | Archive-labelled 24–64-player scale boundary; unresolved author, study only |

Study maps retain their authors' attribution in the manifest and atlas. Titles, capacities and theme labels originate from local archive metadata/readmes and may need correction after inspection. In particular, a package directory may differ from the load name: `aerowalk` loads as `aeroq2`, and the `q2rdm*` maps are packaged together.

## A session that produces useful knowledge

Use about 50–60 minutes per core map for a first pass; repeat gameplay at different capacities instead of stretching one session into a universal verdict. The lesson's question determines which experiments deserve more time.

1. **Five-minute provenance check.** Record the exact BSP, entity variant, engine, game module, physics settings, dmflags, player count and required assets. Check author notes and existing metadata conflicts. Keep source files immutable.
2. **Ten-minute spatial reconnaissance.** Load the exact map in a windowed engine with the required package assets and appropriate game module. Identify landmarks, floors, choke transitions, objectives, special movement and return routes. Name spaces by function, not by BSP area number.
3. **Ten-minute spawn/item audit.** Start from every relevant spawn. Measure time to a useful weapon, recovery resources, first safe choice and first contested space. Trace major-resource circuits in both directions. Record opportunities available to the player without control.
4. **Fifteen-to-twenty-minute gameplay experiment.** Use matched starts and fixed rules. For duel, exchange roles/starts; for team play, swap sides; for FFA, test at the recommended lower and upper loads in separate runs. Record immediate spawn deaths, encounter intervals, resource control, route choices, dead travel, congestion and movement failures. Bot behavior is one source of evidence, not a human enjoyment rating.
5. **Ten-minute review.** Attach entity/space IDs, observed routes and actual evidence to each finding. Separate observation, interpretation and proposed rule. Record at least one counterexample or tradeoff for each proposed recommendation. Leave unobserved dimensions null.

Runtime automation must force supported windowed settings. Rendering evidence must come from the engine's registered `screenshot` command and its render-target output, never OS capture. Agent mouse/keyboard control requires the user's advance permission for that specific action; the study tools do not perform input control or launch a game. The generated load names are a session preparation aid, not evidence of a completed runtime test.

Use anchored ratings only after observing the relevant behavior:

| Rating | Meaning |
| --- | --- |
| 1 | The intended activity repeatedly fails or becomes unusable |
| 2 | Repeated problems obstruct play; a specific cause is documented |
| 3 | Usable for the stated intent and load, with documented tradeoffs |
| 4 | Deliberate choices work clearly and survive matched comparisons |
| 5 | Strong repeated evidence across the intended conditions and independent reviewers |
| null | Not evaluated or evidence is insufficient |

## Rules, experiments and training examples

[rules.json](../knowledge/q2/rules.json) contains 15 initial rule cards. Each records a principle, mechanism, generator action, measurements, counterexample, controlled experiment and evidence status. The cards cover navigation legibility, purposeful loops, spawn recovery, useful verticality, counterplay, distributed resources, combat ranges, space rhythm, coherent theme, readable light, capacity, CTF fairness, movement reliability, performance and portfolio variety.

Promotion follows a deliberate chain:

1. A map observation identifies a possible mechanism and a counterexample.
2. A **project-authored** procedural pair changes one factor, such as an alternate route, an exposed resource, spawn cover or a useful upper destination. Source-map copying is unnecessary.
3. Both variants retain the hard structural/compiler/navigation checks. An intentionally broken variant is labelled with its real defect and never called playable simply to fill a negative dataset.
4. Reviewers compare anonymized variants in randomized order under the same rules and player load. Record wins, ties, disagreements and reasons; do not turn all community references into positives and all generated maps into negatives.
5. A ranking example binds both exact layout/map/BSP hashes, normalized request, compiler/engine identity, experiment, evidence, reviewer label and confidence. Missing evidence stays missing. Separate measured feature vectors from human quality labels.
6. Only reviewed, eligible examples enter a versioned training manifest. Train a small ranker first, evaluate on held-out author/family groups, and compare against the current heuristic. Preserve hard validators around any later constrained planner or repair model. Record data/model provenance and export GGUF only after a real training/evaluation run exists.

The current tools implement the reference registry, static descriptors, descriptive profiles and generator evaluation boundary. They do **not** fit ranking weights, fine-tune a language model, label enjoyment, or import a new model into the C++ runtime.

## Keep variety and quality separate

Author identity, quoted nicknames, named map ancestry, explicitly curated families and exact compiler-geometry hashes are unioned **before** partition assignment. Entity-only versions remain together. Unknown-author components become `study_only`; they cannot silently influence the reference profile. A multi-author remake connects both authors' components, which is deliberately conservative.

The resulting split is provisional: differently compiled, translated, rotated or unrecorded remakes and unrecognized author aliases can evade it. Review lineage before any training/evaluation claim, then freeze the manifest. The atlas contains holdout diagnostics for auditability, so these are prospective partitions, not a claim that nobody has ever inspected those maps. After test results inform a change, retire that test set for the next evaluation.

Profiles distinguish duel, small/medium/large DM capacity groups and CTF. Each author/family component contributes one median per metric, so prolific authors and variants do not dominate. At least five independent components with a measured metric are required for a comparison. Never pad a sparse CTF cohort with duel maps.

Use p10–p90 bands to ask why a candidate differs. A candidate in the middle of every band can still be dull; a good specialist map may sit outside several. Portfolio testing should ask whether seeds change routes, resource decisions, combat ranges and use of height, alongside whether each map remains coherent and playable.

## Generator regression loop

The frozen [prompt/seed matrix](../knowledge/q2/generator-regression.json) contains ten runs: industrial/gothic duel, small/medium FFA and CTF, each at two seeds. The runner records generator/compiler/profile/matrix hashes, exact commands, source/BSP/portal hashes, M3 sidecar reports and logs. It uses the same static extractor on generated BSPs as on references, keeping incompatible layout-IR metrics out of the comparison.

```powershell
python -B -m tools.map_study benchmark `
  --qmg build/windows-clang/qmg.exe `
  --qbsp E:/_SOURCE/_CODE/ericw-tools-2.0.0-alpha11/qbsp.exe `
  --qbsp-sha256 fad5a4dc4daf42773ad9a812349b2667221bec47a920cad10cb821ae442c8eb8 `
  --profile out/map-study/profile.json `
  --output out/map-study-baseline-final
```

Use an empty output directory for each benchmark run; stale maps/pointfiles cannot count as fresh evidence. Generation and compilation failures remain in `benchmark.json` and cause a failing exit status. Compiler success requires the expected tool hash, successful return, no known invalid-brush/leak diagnostics, no `.pts`/`.lin`, a nonempty portal file and a validated IBSP38. This gate does not run final VIS/light or a runtime match; the existing CTest compiler suite supplies independent leak-negative and VIS/light controls.

For an individual compiled candidate:

```powershell
python -B -m tools.map_study compare `
  --bsp out/map-study-baseline-final/duel_industrial_104729/candidate.bsp `
  --profile out/map-study/profile.json `
  --cohort duel `
  --output out/map-study-baseline-final/duel-comparison.json
```

Run the focused extractor tests after importer changes, the fixed benchmark after generation changes, and the existing full project checks before accepting an implementation change:

```powershell
python -B tests/test_map_study.py
cmake --build --preset windows-clang
ctest --preset windows-clang
```

The Python suite is registered in CTest when Python 3.10+ is available. It uses only project-authored binary fixtures under `.agents/tmp/map-study-tests/`, including thin-wall occlusion, unsafe binary envelopes, repeated entity keys, missing light data, directed routes, malformed metadata, split conflicts, holdout leakage, prolific-author weighting and deterministic end-to-end output.

## Rights and provenance

The user-authorized local study can proceed without redistributing the archive. Local availability, an old redistribution notice, or a decompiled `.map` is not used as an automatic training permission. Every source record starts with unknown modification/redistribution/ML terms and disabled training/raw-retrieval eligibility. Source URLs and retrieval dates remain null when the archive cannot establish them. Review exact terms, source history and attribution before admitting any raw material or derived training example to a distributed corpus.

The binary readers are independently implemented from the public [Quake II format contract](https://github.com/id-Software/Quake-2/blob/master/qcommon/qfiles.h), its [movement constants](https://github.com/id-Software/Quake-2/blob/master/qcommon/pmove.c), and NetRadiant Custom's [EAAS file contract](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/tools/mbspc/mbspc/aasfile.h) and [header encoding](https://github.com/Garux/netradiant-custom/blob/449778b3840f8f2062daab15c87cb012f2ac945c/tools/mbspc/mbspc/aas_file.c). No GPL implementation is copied or linked. See [THIRD_PARTY.md](../THIRD_PARTY.md).
