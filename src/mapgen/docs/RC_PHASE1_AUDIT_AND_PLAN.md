# Release-candidate Phase 1: audit and plan

Written 2026-10-15. This document answers Phase 1 of the v1.0.0-rc1 brief. Sections 1–7 audit the project as it stands: the baseline, every claimed gap checked against the code, corrections to the brief's game facts, a draft benchmark over all 17 game × mode targets, 0–5 area scores, and readiness for each release-candidate path. Sections 8–14 are the plan: workstreams in order of player impact, each with measurable acceptance tests, then sequencing, the architectural decisions awaiting approval, release scope, limits and the inputs needed from the user.

**Nothing in this plan has been carried out.** Phase 1 made no architectural change and modified no existing file. Its only additions are the draft benchmark (`tools/benchmark/draft-v0.json`, `run.py`, `README.md`) and local, gitignored outputs under `out/benchmark-draft/`. Those outputs include the per-case results, the summary, the probe scripts cited below (`probes/`) and the full agent evidence records with file:line citations (`phase1-evidence/`).

**Method.** Each claimed gap was audited and then re-checked by an independent verifier who opened every cited line. id-source facts were read from the local source releases, and from the shipped 1996 Q1 `progs.dat` by disassembly where no source text is local. The benchmark was generated through the normal CLI only. It was compiled with full VIS and final light, plus BSPC for Q3, and every count was reproduced by an independent re-parse. Each area score was challenged in both directions. The plan was drafted through three independent lenses, merged, critiqued requirement by requirement against the brief, and revised. Six load-bearing facts were re-checked by hand at the end: the Q2 dedicated-server deathmatch forcing, inert Q2 NOT_COOP, Q3 `map` versus `spmap`, Q3 GT_TEAM-only weapon respawn, `e1u1/ceil1_4` SURF_LIGHT 10000, and the absence of WALL1_1/FLOOR0_1 from every id1 BSP.

Phase 1 reached generation, static analysis and compiler output only. It ran no engine, client or bot. No human has played any generated map.

Evidence levels used throughout: **static** (code reading or static analysis of generated output), **compiler** (qbsp/VIS/light, q3map2, BSPC output), **native** (native movement trial on pinned id code), **engine** (dedicated-server initialization), **human** (human play). Phase 1 itself reached generation, static analysis and compiler output only. It ran no engine, client or bot, and no human has played any generated map.

## 1. Summary

- **Where the project stands.** M0–M3 are complete by the ROADMAP's own definition ("the stated evidence exists"), but the versions exist only in commit messages: there are no git tags and CMake still says 0.4.0. About 126 uncommitted changes add the Quake II study foundation (M4) and the compact Quake II DM/duel generator. M5 is open. The build passes, and `ctest` passes 64/64 with every optional gate configured.
- **Draft benchmark, 138 cases** (17 game × mode targets × 8 prompts, plus 2 showcase probes): G1 generated 138/138, G3 leak-free compile 138/138, G4 full VIS 138/138, G5 final light 137/138, G6 Q3 AAS 40/40, G2 intent match 70/138. **All hard gates: 70/138.** An independent re-parse reproduced every count.
- **Why cases fail.** Every one of the 70 intent matches passed all hard gates. The failures are mode resolution and missing mode entities, plus one final-light timeout. 56 cases target modes the generator cannot represent (co-op, TDM, Q3 bot arena, and Threewave under the draft policy), and 12 more are vocabulary misses on modes it does represent.
- **Contradictions are resolved silently.** 0 of 34 contradictory prompts surfaced a conflict. 17 of the 70 full passes are silent resolutions, so if surfacing were a gate the total would be 53/138. Of all 859 expected requests, 419 were honoured, 36 partial, 259 not honoured, 56 not representable and 89 unverified. None of them produced a finding.
- **Everything is built on one floor.** Room-source floors are [0] in 138/138 maps. No map has a mover, teleporter, jump pad, liquid or sky, and 0 of 18 "N floor levels" requests were honoured. Only Q2 DM/duel (23/138 maps) uses a real motif vocabulary. At a fixed seed, the legacy path produces the same geometry for Q1 DM, Q3 DM and Q3 duel.
- **Biggest player-facing gaps.**
  - Wrong game mode with no warning.
  - Q1 maps are effectively untextured: WALL1_1 and FLOOR0_1 are not stock names and no `wad` key is written.
  - Q1 DM/CTF matches would end in a fatal QuakeC error because no intermission spot exists (static reading).
  - Item economy is a round-robin placeholder.
  - Single-player has no progression, skill levels, ammo or co-op.
  - Compiled lighting is unreadable: Q1 floors are near-black, and every Q2 map is a flat 174/255 orange from accidental ceiling emitters.
- **Area scores (0–5, overall):** A prompt → brief 2, B classic-map study 3, C layout 2, D items/spawns 2, E single-player 2, F materials 1, G lighting 2, H validation 3. No area scores above 3, and no score changed when challenged.
- **Evidence reached by game.**
  - Q1: compiler output only.
  - Q3: compiler output plus global BSPC AAS counts.
  - Q2: native movement trials and dedicated-engine initialization, but only for DM. Those maps are recipe maps plus six default-generator studies that predate the current build.
  - No connected-player run of any kind (human, bot or scripted) exists.
- **The brief's game facts need corrections before encoding.** Q3 uses 30 s weapon respawn only in GT_TEAM, not CTF. Q2 NOT_COOP (4096) is inert in 3.19. Re-release Q1 Mega Health respawns a fixed 120 s after pickup. The 1996 Q1 progs respawn weapons, armor and ammo only when `deathmatch == 1`. Details are in section 4.
- **Release-candidate readiness.** No claimed path meets all five RC conditions, and RC3 (a hero chosen in advance) and RC5 (plain docs) are unmet everywhere. Recommendations: keep 6, keep if inputs are supplied 5, decide later 5, likely drop 1 (Q1 Threewave CTF).
- **Nothing in the repository was changed.** The only additions are `tools/benchmark/` (manifest, runner, README, inside the already-untracked `tools/` tree) and local outputs under the gitignored `out/benchmark-draft/`. `git status --short` still shows 126 entries, and `git diff --shortstat` still shows 35 files, +2454/−54.

## 2. Baseline

| Item | Fact | Level |
|---|---|---|
| Build | `cmake --build --preset windows-clang` exit 0 (incremental, 23.6 s, 93 steps incl. llama.cpp). qmg.exe sha256 `815bb58d…`, the binary the benchmark used. | compiler |
| Tests | `ctest --preset windows-clang -j 6`: 64/64 passed in 101.7 s. Optional gates configured: Q1/Q2 ericw-tools 2.0.0-alpha11 qbsp/VIS/light (hash-pinned), Q3 q3map2 + mbspc (netradiant-custom 449778b, pinned), tokenizer GGUF, real GGUF model (SmolLM2-135M-Instruct-Q8_0). `qmg_native_movement_tests` skips when the local executable is absent (CMakeLists.txt:308-311), so a pass does not prove a native run. Two CTest groups read the gitignored `out/`. | compiler |
| Uncommitted tree | 126 porcelain entries: 35 modified tracked files (+2454/−54) and 91 untracked entries (about 220+ files with `-uall`). The scope is wider than "Weft → Span": the whole M4 Q2 study foundation, the recipe iteration chain (Counterweight to Span), native Pmove/teleport harnesses, the QL atlas reader, core fixes, and the compact Q2 DM/duel builder (mp_motifs, spawn_refinement, upper_links). The brief asks for approval before committing this as a baseline. | static |
| Concurrent writes | During the audit, Phase 1's own `tools/benchmark/` files were being written, so untracked-file counts drifted (257 → 259). These files are not part of the user's earlier work. | static |
| Version and provenance | No git tags. `generator_version` 0.4.0 does not identify the code revision. M3-era and current Q2 DM output carry the same version. ROADMAP:55 says "15 rule hypotheses" against 60 cards. | static |
| Determinism | With the default rules/v1 planner, two CLI runs with the same raw prompt, overrides and seed give byte-identical `.map` and `.qmg.json` (re-verified for q2 dm, q3 ctf brush-primitives and q1 sp, and probed across 7 cases × 2 runs with locale independence). **Nuance:** the key is the raw prompt text, not a normalized request. `append_prompt` hashes `source_prompt`, title and theme into `layout_sha256` (src/layout_fingerprint.cpp:87-92), so a case or whitespace variant gives an identical `.map` and a different report. Only one in-process byte test exists (tests/test_main.cpp:214-232, Q1 DM). GGUF-planner repeatability and cross-machine float determinism are untested. | static |
| Correctness fixes only in the uncommitted tree | (1) At HEAD (M3 commit 1252548), Q2 door and lift destinations were lowered to `target_position`, which is not a Quake II spawn class. The working tree uses `info_notnull` (src/map_builder.cpp:965, 971; Quake-2-master/game/g_spawn.c:95, 215). (2) At HEAD, navigation tiled ramp brushes as flat supports at their maximum height (navigation.cpp:221-223, 1310-1313). The working tree adds a slope model (`NavigationSlope`, include/qmg/navigation.hpp:86). Both M3 claims therefore rest on uncommitted code. | static |
| Compiler gates | Q1 sp6/dm32 (BSP29) and Q2 sp6/ctf32 (IBSP38) run a quick (`vis -fast`) and a final (full VIS, `light -extra4 -bounce 1 -soft`) stage. Q3 duel4/ctf8/dm32 in both dialects run IBSP46 + EAAS5, with final VIS/light only on duel4. Every gate has a live negative leak control. Caveats: `-notex`/`-nodefaultpaths`, `--no-report` (findings never checked), and weak completion markers (`calculating full vis` is also printed by `-fast`, and the regex `0 empty lightmaps` also matches `10 empty lightmaps`). The default compact Q2 DM/duel path has no compiler gate. | compiler |
| Local data | Q1 id1 (51 BSPs, no texture WAD; gfx.wad is the HUD wad), Q1 re-release, Q2 baseq2 (loose WALs, q2dm1-8, base units), Q2 CTF (q2ctf1-5), Q2 re-release, Q3 baseq3 extracted (shaders, q3dm0-19, q3tourney1-6, q3ctf1-5, pro- revisions, all with .aas), and the Q2 community archive (396 entries). id source releases for all three games are local, plus Quake3e, fteqw, ezquake and q2repro checkouts and an ironwail binary. | static |

## 3. Gap audit

Each claimed gap from the brief was checked by an auditor and then by an independent verifier. The verdict shown is the verifier's final verdict. "Partially confirmed" means the gap is real but the brief's wording is wrong in a way that matters; the precise statement gives the corrected version.

### Prompt and modes

| ID | Claim (short) | Verdict | Precise statement | Key evidence |
|---|---|---|---|---|
| P1 | PromptSpec holds theme, rooms, players, verticality, intensity, loops, wrapped_center | Partially confirmed | Also holds game, mode, seed, title, source_prompt. Verticality is one of 3 keyword values (0.95/0.15/0.75) or the default 0.5; intensity one of 2 (0.9/0.25) or 0.5. No field for lighting, items, liquids, mechanisms, size, difficulty or keys. Nothing marks a field as defaulted. CLI overrides only game, mode, seed, rooms, players (+ `--format`). | include/qmg/types.hpp:48-61 PromptSpec; include/qmg/prompt.hpp:10-16 PromptOverrides; src/prompt.cpp:268-280 PromptParser::parse |
| P2 | SemanticPlan holds route style, symmetry, lighting style, room roles | Partially confirmed | Also holds verticality/intensity 0-100; RoomRole has 12 values. `symmetry` is read only by validation and a wrapped_center throw, never by geometry, so "strict" CTF plans are not mirrored (rooms are sized independently). `route_style` is read only by the SP connector. `lighting_style` changes room-light brightness and colour. | include/qmg/semantic_plan.hpp:15-55; src/mp_motifs.cpp:372-375; src/generator.cpp:236-238, 256-260 |
| P3 | Prompt cannot ask for items, landmarks, liquids, mechanisms, sky, difficulty, keys, secrets | Confirmed | No parser vocabulary, no plan field, nothing in the GGUF output schema, and no generator path for any of these. Requests are dropped without a finding. LiquidSpec and the door/lift/teleporter specs are used only by construction.cpp and tests. | src/prompt.cpp:223-304; src/semantic_planner.cpp:61-67 system_prompt; src/construction.cpp:548-558 |
| P4 | Theme by keyword, quiet fallback to industrial | Confirmed | Raw substring scan of 14 needles (11 labels) in table order, not prompt order. No match gives "industrial", and the report does not say whether the theme matched or defaulted. False positives verified: "database" and "two bases" → techbase, "open space" → space. Theme never selects materials. | src/prompt.cpp:173-186 detect_theme; src/report.cpp:607-609; tests/test_main.cpp:80-92 |
| M1 | GameMode = SP, DM, duel, CTF | Confirmed | Exactly four enumerators. `--mode` accepts sp, dm, duel, ctf. | include/qmg/types.hpp:16-21 GameMode; src/main.cpp:163-173 |
| M2 | No TDM, no co-op | Confirmed | No enumerator, keyword or entity. "team deathmatch" → FFA (Q2: 8 DM spawns, no team entities). "Quake 1 co-op for 4 players" → SP with one start. A bare "tdm" → Q1 monster campaign. | src/prompt.cpp:204-221 parse_mode; src/entities.cpp:244-247 |
| M3 | Q1/Q2 CTF declare no game module | Partially confirmed | Q2 CTF classnames match the official ctf DLL and are absent from baseq2, but no ctf gamedir is declared and no warning is raised. Q1 CTF raises `q1_ctf_mod_profile`, which never names Threewave or `quakec_ctf`. Neither report nor worldspawn has a mod/gamedir field. | src/validator.cpp:450-473; src/map_builder.cpp:923-949 classname_for; Quake-2-master/ctf/g_ctf.c:441-466 |

### Construction

| ID | Claim (short) | Verdict | Precise statement | Key evidence |
|---|---|---|---|---|
| C1 | Only Q2 DM/duel uses the compact motif vocabulary | Partially confirmed | True: only Q2 DM/duel with 2-32 rooms enters `build_compact_mp_layout`, and a test enforces it. Correction: Weft, Splice, Span and Hinge are iteration names in docs, tools and tests, not code. The real vocabulary is 2 families (rows, pinwheel), 3 elevated motifs chosen by index parity, an `offset_barrier` in each flat room, and one optional upper link (needs wrapped_center + verticality ≥ 0.3; wrapped_center itself is allowed only for Q2 DM/duel with 5–8 rooms). | src/generator.cpp:223-226; src/mp_motifs.cpp:372-386, 444, 501-503, 526-536; src/upper_links.cpp:232-241; tests/test_mp_motifs.cpp:335-341 |
| C2 | Q1/Q3 MP and all SP use the older path | Confirmed | Q2 SP and Q2 CTF do too. The legacy path builds random boxes in a two-row ring (every non-SP mode and every Q3 request) or a serpentine chain (SP), joined by straight corridors, with one-stair ledges. At seed 42 and 6 rooms, the sorted brush plane points of Q1 DM, Q3 DM, Q3 duel and a Q3 "jump pads, lava, lifts, teleporters" prompt hash identically. | src/generator.cpp:228-264 (230); src/entities.cpp:347-351 |
| C3 | Lifts, teleporters, liquids, ramps, drops exist but are never selected | Partially confirmed | Lifts, teleporters, liquids, ramps, doors, arches, curves (with a Q3 patch veneer) and stairs are typed requests. Drops are not: they exist only as a navigation transition and as recipe `DropSpec` notes. The default pipeline cannot reach any of them: there is no plan field, `ConstructionBuilder` is called only from recipes and tests, and the two-argument `MapBuilder::build` passes an empty `ConstructionResult{}`. Recipes are hard-wired to Q2 DM and cannot be reached from `qmg generate`. No recipe uses doors, arches or curves. | include/qmg/construction.hpp:139-140; src/pipeline.cpp:101-103; src/map_builder.cpp:1486-1488; tools/map_recipes/recipe_support.hpp:48-51, 72, 127 |
| C4 | No Q3 jump pads | Confirmed (raised from partial) | There is no push PlacementRole, no jump-pad spec and no `trigger_push` case in the classname switch. `target_position` is emitted only for Q3 door/lift destinations, which default generation never creates. `trigger_push` appears only in read-only reference tools. | include/qmg/layout.hpp:463-484 PlacementRole; src/map_builder.cpp:960-975; tools/ql_atlas/bsp.py:163-164 |

### Items and spawns

| ID | Claim (short) | Verdict | Precise statement | Key evidence |
|---|---|---|---|---|
| I1 | Weapons round-robin, one per room | Partially confirmed | Arena allocation (all Q1/Q2 MP, all Q3 including Q3 "SP") places one weapon per room, variant = room index % list length (5 for Q1, 7 for Q2/Q3). Q1/Q2 SP places weapons in odd-index rooms only. At the default 6 rooms, Q2 never gets a railgun, Q3 never gets a plasmagun, and Q1 repeats the nailgun. With 3 or fewer rooms, no game gets a rocket launcher. | src/entities.cpp:214-219 populate_arena, 79-88, 269-272; include/qmg/types.hpp:55-56 |
| I2 | Health always item_health | Confirmed | Plain `item_health` with no spawnflags: 25 HP in Q1, 10 HP in Q2, 25 HP in Q3. One per room (arena) or per non-start room (SP). Extra health for "resource" rooms fires only on GGUF plans. | src/map_builder.cpp:934-935, 1076-1145 make_placement_entity |
| I3 | One armor class per game | Confirmed | Q1 `item_armor2`, Q2 `item_armor_combat`, Q3 `item_armor_body`. Arena only: one per raised-platform room, plus "control" rooms (GGUF only). SP gets no armor. | src/map_builder.cpp:936-944; src/entities.cpp:222-228 |
| I4 | No ammo, MH, shards, powerups | Confirmed | The only item roles are weapon, health and armor; only weapon and monster carry a variant. There is no ammo, shard, Mega or other health tier, powerup or holdable, and no item spawnflags. | include/qmg/layout.hpp:463-484; src/validator.cpp:250-256; src/map_builder.cpp:1070-1073 |
| I5 | Missing Q1 SSG, Q2 SSG/BFG, Q3 BFG | Confirmed | Q2 hand grenades (`ammo_grenades`) are also missing. The blaster and gauntlet are correctly omitted, but Q3 spends a slot on `weapon_machinegun`, which every Q3 player spawns with. | src/map_builder.cpp:876-883 variant_class; Quake-III-Arena code/game/g_client.c:1179 |
| I6 | Placement ignores strength, control/recovery, respawn timing | Partially confirmed | Strength and timing are ignored entirely, and SP order is not monotonic (the Q1 RL in room 3 comes before the nailgun in room 5). Role awareness is minimal: weapon and armor sit on raised platforms, and compact Q2 binds them to upper-deck stations. The control and resource branches are unreachable under rules/v1. No `wait`, `random`, spawnflags or team keys are written. | src/entities.cpp:112-119, 217-232; src/semantic_plan.cpp:343-363; src/mp_motifs.cpp:302-304 |

### Single-player

| ID | Claim (short) | Verdict | Precise statement | Key evidence |
|---|---|---|---|---|
| S1 | Monsters rotate through 5 types at fixed spots | Confirmed | Q1 army/knight/ogre/demon1/shalrath; Q2 soldier/infantry/gunner/berserk/gladiator. Every non-start room, the exit room included, gets clamp(round(intensity·4), 1, 4) monsters at fixed slots (x 25/75%, y 32/68%). Class is (room + slot) % 5 and facing is (index·73 + slot·91) % 360. The seed changes only room sizes and platform heights. | src/entities.cpp:244-262 populate_single_player; src/map_builder.cpp:884-887 |
| S2 | No themed monster sets, skill flags, ambushes | Confirmed | The allocator never reads the theme. `Placement` has no spawnflags field, so no skill, ambush or trigger_spawn flag is ever written. For comparison, stock e1m1 has 32 not_easy monsters and 7 ambush; base1 has 16 of 19 monsters on ambush. | include/qmg/layout.hpp:528-542 Placement; src/map_builder.cpp:1076-1108 |
| S3 | No keys, locked doors, secrets, weapon progression | Partially confirmed | No keys, doors of any kind or secrets on the SP path; `func_door` comes only from the construction API, which the pipeline never calls. Weapons do appear (odd rooms, index % count), but ignore encounter difficulty. Neither roster has the SSG, and Q2 has no BFG. `DoorAssemblySpec`'s "key" field is an identifier, not a lock key. | src/entities.cpp:269-272; src/construction.cpp:576; include/qmg/layout.hpp:463-484 |
| S4 | Exit hardcoded to start/base1 | Confirmed | Q1 `trigger_changelevel` map "start"; Q2 `trigger_once` → `target_changelevel` "base1" (no `*`, no `$spawnpoint`). No CLI flag or prompt phrase changes it, and the validator checks only that it is non-empty. | src/entities.cpp:323-325, 330-336; src/validator.cpp:425-445 |
| S5 | No Q2 unit/hub | Confirmed | Each level is standalone and one-way: one untargeted start and one exit. There are no named arrivals, return changelevels, `info_player_coop`, intermission, `target_help`, `target_goal` or `target_secret`. A `map$name` arrival would hit "Couldn't find spawn point". The Q1 intermission class is `info_intermission`. | src/entities.cpp:245-246, 318-337; Quake-2-master/game/p_client.c:884-905 SelectSpawnPoint |

### Materials

| ID | Claim (short) | Verdict | Precise statement | Key evidence |
|---|---|---|---|---|
| T1 | One texture per role per game | Partially confirmed | Materials are a pure function of (game, role, contents). The 13 roles collapse to 4 names per game. Every CLI map of a game uses the same 3 visible textures: Q1 WALL1_1/FLOOR0_1/CEIL1_1; Q2 e1u1/flat1_1, floor3_1, ceil1_4; Q3 comp3text, clang_floor, pewter_shiney. There is no texture or wad option. | src/map_builder.cpp:242-313 solid_material, 359-368 material_for |
| T2 | Theme only written as _qmg_theme | Partially confirmed | Theme has no effect on materials, liquids, sky or geometry. It also reaches the worldspawn message, the report, the layout hash, the GGUF prompt and the compact request-equality tuple. "gothic lava castle" → gothic, with no liquids. | src/map_builder.cpp:1577-1579; src/prompt.cpp:173-186, 299 |
| T3 | No trims, light textures, sky, accents | Partially confirmed | True for deliberate ones. Correction: the Q2 ceiling `e1u1/ceil1_4` is a SURF_LIGHT value-10000 WAL, and solid faces carry no Q2 metadata, so every Q2 ceiling is an unmanaged area light. In seed-3 DM that gives 152 surface-light points and a mean lightmap of 152.0, against 20.2 with explicit `0 0 0`. Sky and accents exist only in recipes. | src/map_builder.cpp:274, 438-439; E:/_SOURCE/_ASSETS/Q2/textures/e1u1/ceil1_4.wal; tools/map_recipes/recipe_support.hpp:176-244 |
| T4 | Q1/Q2 hidden faces visible wall texture, not skip | Confirmed | On shell slabs the room-facing face gets the role texture and the other 5 get WALL1_1 or e1u1/flat1_1 (Q3: caulk). Non-shell boxes get the role texture on all 6 faces in every game, so risers carry the floor texture and Q3 buried faces are not caulked. Stock Q2 ships e3u1/skip and e2u3/skip. | src/map_builder.cpp:255-261, 277-283, 299-300, 468-490, 801-814 |
| T5 | Q3 liquids use unshipped qmg/* shaders | Confirmed | qmg/water, slime and lava exist nowhere: not in the repo, baseq3, or the 36 stock shader lumps. q3map2 takes contents from shaderinfo, so a Q3 liquid would compile as a solid default-shader brush. This affects library and recipe callers only, since the CLI emits no liquids. | src/map_builder.cpp:343-353; q3map2 map.cpp:1007-1018 ParseRawBrush; cmake/RunQ3CompilerTest.cmake:51-80 |

### Lighting

| ID | Claim (short) | Verdict | Precise statement | Key evidence |
|---|---|---|---|---|
| L1 | One point light per room, 64 below ceiling, fixed brightness | Partially confirmed | True for every Q1 mode, Q2 SP/CTF and every Q3 mode: one light at the XY centre, max.z − 64, with intensity set by game and style (Q1/Q2 220/320/450, Q3 300/450/600). Q2 DM/duel also adds 260 and 280 lights per elevated motif room and a 240 light per connection, in a fixed cool colour that ignores lighting_style (Weft: 21 lights for 6 rooms). Room size, height and theme never change any light. | src/entities.cpp:154-175 add_lighting; src/mp_motifs.cpp:77-85, 539-550 |
| L2 | No fixtures, sun/sky, surface lights, styles | Partially confirmed | Lights are plain `light` entities with origin, `light` and `_color` only. There are no light_* fixtures, styles, targetnames or worldspawn light keys. Correction: Q2 does emit surface lights, by accident, through ceil1_4 (191 points in the Weft BSP). Sun and sky exist only in the Lockyard recipe. | src/map_builder.cpp:950-951, 1105-1108, 1574-1579; out/weft/qmg_weft.bsp texinfo |
| L3 | Outstanding analytical lighting warnings | Confirmed | Every hero map keeps warnings (Weft 2 … Span 6; Lattice also has an unreadable_sample at brightness 0). The packaged span-portfolio map rooms5-seed733 carries an ERROR (`unlit_critical_placement`, a spawn at 468 against a threshold of 500). LightingAnalyzer runs after ranking and repair, and nothing reads its findings. | src/pipeline.cpp:69-121 (117); out/span-portfolio/generated/rooms5-seed733 |

### Evidence and tools

| ID | Claim (short) | Verdict | Precise statement | Key evidence |
|---|---|---|---|---|
| E1 | 60 cards, Q2 MP only, says it does not transfer | Partially confirmed | 60 cards scoped to Q2 multiplayer (DM/duel plus one CTF card), none for SP. The file does not say the cards fail to transfer; it says transfer "requires separate evidence". The wider tree has 40 JSON files, including knowledge/mp/spatial-patterns.json (7 QL and 2 Q2 references, scoped to Q2 design). | knowledge/q2/rules.json:3-4, 170-183; knowledge/mp/spatial-patterns.json:6, 22 |
| E2 | map-study reads only IBSP38 | Partially confirmed | `Bsp` accepts only Q2 IBSP38; q3dm17 and e1m1 were both rejected by probe. The same module reads EAAS v4/v5. A separate tools/ql_atlas reads IBSP46/47 for six QL maps; it parses and counts brushes but does not use them for collision. No tool reads BSP29. | tools/map_study/binary.py:178-181 Bsp.__init__, 321-332; tools/ql_atlas/bsp.py:57-95 |
| E3 | Native movement and engine smoke Q2 only | Confirmed | The harness refuses anything but IBSP38 and links 8 pinned Q2 3.19 files. native_teleport hosts the Q2 3.19 game module, CM and Pmove. engine_smoke.py pins q2reproded.exe and gamex86_64.dll. Nothing runs a Q1 or Q3 engine, Pmove or bot; Q3 has only BSPC aasinfo counts. | tools/native_movement/harness.c:68-69; tools/map_recipes/engine_smoke.py:29-32, 65-66; cmake/RunQ3CompilerTest.cmake:155-276 |
| E4 | No human playtesting | Confirmed | No human playtest is recorded. The study-plan template has 26 sessions, all with reviewer and scores null. Counterweight's regimen is "proposed, not executed". No connected-player run of any kind exists. Informal, undocumented play cannot be ruled out. | knowledge/q2/rules.json:4; out/map-study/study-plan.template.json; docs/Q2_COUNTERWEIGHT_STUDY.md:136 |

### Baseline and milestones

| ID | Claim (short) | Verdict | Precise statement | Key evidence |
|---|---|---|---|---|
| B1 | M0–M3 complete; M4 Q2-only; M5 open | Partially confirmed | The ROADMAP marks M0–M3 complete (v0.1.0–v0.4.0), but those versions exist only in commit messages. Two M3 defects are fixed only in the uncommitted tree (section 2). M4 is an uncommitted Q2-only study foundation with no rights-reviewed corpus, labels, index or training. ROADMAP:55 is stale. The M5 bullets have no status. | docs/ROADMAP.md:3, 7, 15, 25, 38, 55, 62-66; CMakeLists.txt:3; src/map_builder.cpp:965, 971 |
| B2 | ~126 uncommitted changes, Weft → Span | Partially confirmed | 126 entries (35 modified, +2454/−54; 91 untracked). The scope is much wider than Weft → Span (section 2). | git status --short; knowledge/q2/iteration-roadmap.json; src/upper_links.cpp:235-236 |
| B3 | Deterministic for fixed normalized request, model, seed | Partially confirmed | Byte-identical for a fixed raw prompt under rules/v1. The key is the raw prompt, not a normalized request. One in-process byte test exists. GGUF repeatability and cross-machine float determinism are untested. | src/layout_fingerprint.cpp:87-92 append_prompt; tests/test_main.cpp:214-232 |
| B4 | CTest compiles Q1/Q2/Q3, full VIS/light on some fixtures | Confirmed | As listed in section 2, with the caveats: `-notex`, `--no-report`, and weak VIS/light completion markers. | cmake/RunClassicVisLight.cmake:101-106, 164-168; cmake/RunQ3CompilerTest.cmake:495-499 |

### Gaps the brief did not list

These merge the auditors' additional gaps that the verifiers confirmed (real = true) with the gaps the verifiers found the auditors had missed. Rows are ranked by player impact, highest first. Items that only restate a claimed gap are folded into that claim and not repeated. Folded this way: SP ammo starvation (I4/S3), unhonoured feature requests (P3/C3), symmetry not realized (P2), no sky volumes (P3/T3), SP campaign chaining (S4/S5), weak VIS/light regexes (B4), and the Q2 ceil1_4 emitter (T3). Unless stated otherwise, runtime consequences come from reading id source code and have not been tested in an engine.

| Gap | Player impact | Evidence |
|---|---|---|
| **1.** Q1 DM, duel and CTF maps have no `info_intermission` or `info_player_start` | When a fraglimit or timelimit ends the match, FindIntermission finds no `info_intermission`, `info_player_start` or `testplayerstart` and calls objerror, a fatal "Program error". This holds in both progs, by static reading. A generated Q1 MP match on a server with a fraglimit or timelimit would therefore end in a server error. A server with neither never reaches this code. Not engine-tested. | Re-release quakec/client.qc:135-168 FindIntermission; shipped 1996 progs.dat FindIntermission statements 5672-5715, reached from execute_changelevel at statement 5878 (disassembly); WinQuake/pr_cmds.c:81-94 PF_objerror; src/visibility.cpp:238-239 |
| **2.** Q1 primary textures WALL1_1 and FLOOR0_1 exist in no id asset, and no worldspawn `wad` key is written | About 80% of compiled faces have no miptex (80.7% in a probe even with a wad key added). This is hidden because CTest and the benchmark compile with `-notex`. | src/map_builder.cpp:250, 261 solid_material; out/benchmark-draft qbsp logs ("No wad" ×48, "unable to find texture" ×48) |
| **3.** Co-op prompts produce SP maps with a single `info_player_start` | Players 2 and up fall back to that start and are telefragged (Q2 KillBox, Q1 spawn_tdeath). There is no intermission camera. | src/entities.cpp:244-247; Quake-2-master/game/p_client.c:831-865, 1247 |
| **4.** Q3 CTF emits one `team_CTF_redplayer`/`blueplayer` per team | With 3 or more per team (the CTF minimum is 6 players), team-begin spawns pile onto one spot and players telefrag on join. | src/entities.cpp:195-197 add_arena_spawns; Quake-III-Arena code/game/g_team.c:983-1046 |
| **5.** Q1/Q2 MP maps have no `info_player_start` | `map <name>` without deathmatch set aborts (Q1 "no info_player_start", Q2 "Couldn't find spawn point"). | src/visibility.cpp:238-239 role guard |
| **6.** At "brutal" intensity, large monsters spawn embedded in platforms | Ogre, fiend, vore and gladiator boxes overlap platforms by 12–16 units, so they are likely stuck or removed. | src/entities.cpp:51-64, 253-262 supported_point |
| **7.** SP pickups are placed by point test only | In the seed-5 probe, exit-room health overlaps a platform and droptofloor would remove it. | src/entities.cpp:51-64, 273 |
| **8.** A raised platform can block the north doorway and leave a 64-unit strip | The player must detour around the platform, and hull-2 monsters are reported unable to reach the player from the last two rooms. | src/generator.cpp:153-169 add_vertical_features |
| **9.** Contradictory and multi-mode prompts are resolved by fixed precedence (ctf > duel > dm > sp) and silent coercion (duel forces 2 players; CTF forces at least 6) | The hero's "2–8 player DM that also works for 1v1" becomes a 2-spawn duel, and "q3dm17 remake for Quake 2" becomes Quake III. 0/34 benchmark contradictions were surfaced. | src/prompt.cpp:204-221 parse_mode, 289-295, 192-200 parse_game |
| **10.** Common mode words are not recognised: tourney, tournament, ffa, tdm, teamplay, bot arena, one-on-one, two players, LAN party | Players get the wrong game type with no hint why. A game-less "ffa" or "tdm" gives a Q1 monster campaign. | src/prompt.cpp:209-218, 249-252 |
| **11.** Map-name references such as "q2dm1" are read as single-player and can switch the game | "A q2dm1 style map" gives a Q2 campaign; "Quake 1 remake of q2dm1" gives a Q2 campaign. | src/prompt.cpp:27-38, 212 contains_word |
| **12.** Unsupported requests are dropped, values clamped and defaults unmarked; spelled-out numbers are ignored | 259 not-honoured and 89 unverified requests out of 859 produced zero findings. "Eight players" gives 4 spawns. The report has no `target_players` field. | include/qmg/types.hpp:48-61; src/prompt.cpp:109-142, 258-263; src/report.cpp render_report |
| **13.** Negation and substring false positives | "No loops" adds a loop, "not vertical" raises verticality, and "no water" gives the waterworks theme. | src/prompt.cpp:268-274, 297-298 |
| **14.** Q3 "single_player" is an unlabelled FFA arena | "Campaign" prompts report single_player but get 4 DM spawns, no .arena script and no intermission point, with no warning. | src/semantic_plan.cpp:289-291; src/entities.cpp:347 |
| **15.** Legacy geometry ignores almost all prompt content, and verticality 0.3–0.8 consumes identical RNG draws | For Q1, Q3, Q2 SP and Q2 CTF, different prompts at the same seed and room count give the same map. | src/generator.cpp:149-158 |
| **16.** rules/v1 ignores lighting and role vocabulary, and there are no CLI overrides for theme, lighting, verticality, intensity or loops | "Dark" or "moody" changes nothing without a model, and resource, control and setpiece rooms are never planned. A misparse cannot be corrected from the CLI. | src/semantic_plan.cpp:338-360 make_rule_semantic_plan |
| **17.** Analysis errors never gate or repair output, and compiler fixtures run with `--no-report` | The 8-room Q1 SP map ships 29 navigation errors while the CLI prints "validation=passed". The Q3 ctf8 fixture passes with a flag the analyzer reports unreachable. | src/pipeline.cpp:85-123; cmake/RunQ3CompilerTest.cmake:411-421 |
| **18.** Repair re-runs a deterministic allocator on an unchanged layout | Placement errors persist. On compact Q2 the re-run would also drop station binding, both spawn refinements and the motif lights (latent: no probe triggered a repair). | src/repair.cpp:450-458 |
| **19.** Lighting findings never feed ranking or repair | Dark spawns ship: the delivered span-portfolio seed733 map carries an unlit_critical_placement ERROR. | src/pipeline.cpp:69-121 |
| **20.** CTF halves are not mirrored, in item sets or in flag offsets | In a Q2 6-room CTF map, red gets SG/MG/CG and 1 combat armor while blue gets GL/RL/HB and 2. One flag sits nearer the centre. | src/entities.cpp:216-224, 235-240 |
| **21.** CTF team spawns span the whole half, and flag rooms hold a weapon about 156 units from the flag | Attackers respawn a doorway away from the enemy flag, and carriers can re-arm at the flag. | src/entities.cpp:182-193, 217-219 |
| **22.** Q1 CTF emits no `info_player_team1/2` | First spawns land on DM spots across both halves. The map is playable only through the QC fallback to DM spawns. | src/entities.cpp:179; quakec_ctf/client.qc:535-547 |
| **23.** The default room count suppresses top weapons | With 6 rooms there is no Q2 railgun and no Q3 plasmagun; with 3 or fewer there is no RL in any game. | include/qmg/types.hpp:55; src/entities.cpp:217-219 |
| **24.** Armor placement follows platforms | A Q3 6-room DM gets 3 Red Armors and a Q1 one gets 3 Yellow, so armor has no control value. | src/entities.cpp:222-224 |
| **25.** Spawn count equals player count, facing is an index rotation, and rooms are assigned by index % rooms, ignoring the "spawn" role | A duel has 2 known spawns, players respawn facing walls, and the spawn heat-map skews to low-index rooms. | src/entities.cpp:178, 187-210 add_arena_spawns |
| **26.** No spawn-to-item sightline or proximity rule exists, and every spawn sits in a weapon room | Spawns double as item camps. | src/visibility.cpp:576-587, 1057-1087; src/entities.cpp:217-219 |
| **27.** Spawn LOS refinement exists only for compact Q2 DM/duel (2–8 rooms), and Q1/Q3 spawns are sampled at eye offset 0 | Q1, Q3 and CTF spawns are not optimised, and their safety analysis uses the wrong eye height. | src/generator.cpp:223-226; src/visibility.cpp:278-282 |
| **28.** Items carry no entity keys (spawnflags, wait/random, team, Q3 gametype/notfree/notteam) | There are no rotten or mega variants, suspended items, team-only sets or tuned respawns, and one Q3 map cannot serve several gametypes. | src/map_builder.cpp:1083-1143; Quake-III-Arena code/game/g_spawn.c:412-468 |
| **29.** The SP loop edge links the start toward the exit | A 6-room loop level can be finished in about 4.5–5.2 s, skipping most encounters. | src/generator.cpp:137-147 connect_single_player |
| **30.** SP encounters ignore room role and escalation, and monster facing is arithmetic | An ogre or gunner appears in room 2 while later rooms cycle back to grunts. Some Q1 monsters will not wake on entry. | src/entities.cpp:253-262, 261 |
| **31.** The exit is an invisible 64×192×128 trigger with no landmark or prerequisite, and the Q2 target point is mis-scored as a navigation objective | Players cannot find the exit by sight, and the analyzer raises a false unbound_placement error. | src/entities.cpp:280-322 |
| **32.** Monster navigation hull is not matched to classname, and unbound errors are counted three times | Real hull-2 blockages are downgraded to warnings, and error totals (29/32) overstate distinct problems. | src/pipeline.cpp:107-115; src/navigation.cpp:245-253 |
| **33.** Legacy verticality is one stair to a dead-end ledge, with every room and passage at z = 0 | High ground has one entry, so it is easy to camp, and there are no overlooks or over/under routes. | src/generator.cpp:149-192 add_vertical_features |
| **34.** The compact room graph is fully determined by the partition: a doorway in every shared wall, uniform 160×192 passages at z = 0, and motifs chosen by index parity | Maps read as grids of rooms with doors in every wall, with no designed chokes, flanks or overlooks, so different seeds play alike. | src/mp_motifs.cpp:150-155, 461-466, 488-503 |
| **35.** Q2 DM above 8 rooms gets reduced treatment | Large and team-sized maps get the rows family only, with no pinwheel, no spawn-separation refinement and the legacy proxy ranker. | src/mp_motifs.cpp:334, 385-386 |
| **36.** The ranker selects by seeded tie-break: item features are saturated, and there are no encounter or SP features | "Best of N" is a lottery. Quiet, default and brutal SP maps all score 3.7296. | src/ggml_ranker.cpp:56-135 |
| **37.** No construction accounts for movement technique | Nothing designs strafe lanes, rocket or plasma jump shortcuts, or jump-pad arcs. | include/qmg/navigation.hpp:41-44 NavigationAgentProfile |
| **38.** The Q3 wall (comp3text, a blend layer) and the Q2 wall (flat1_1) are near-black | Q3 rooms read as black boxes, and Q2 interiors are dark except for the glowing ceilings. | src/map_builder.cpp:277-283, 301-306 |
| **39.** Risers and height changes keep the floor texture, and texture scale ignores texture size | Height changes lose their route-reading cue, and 16×16 ceiling tiles repeat every 16 units. | src/map_builder.cpp:246-294 |
| **40.** Q2 liquid names do not exist, and the emitted metadata strips WARP/TRANS (latent: the CLI emits no liquids) | Library and recipe liquids render as missing, opaque and non-warping. | src/map_builder.cpp:331-342 liquid_material |
| **41.** There is no asset-existence or material-physics check; tests assert the broken names; the Q3 test shader's trigger differs from stock | Missing or wrong textures ship while ctest stays green. | src/map_document.cpp:68-78; tests/test_main.cpp:774; cmake/RunQ3CompilerTest.cmake:75-79 |
| **42.** The analytical lighting model matches neither compiler (inverse-square, where ericw uses linear falloff and q3map2 uses pointScale), and no check reads compiled light | Maps are reported "readable" (Q1 92.6%) while compiled Q1 floors are near-black. | src/lighting.cpp:241-250, 330-342; cmake/RunClassicVisLight.cmake:102-108 |
| **43.** Lighting samples are not tested against solids and miss raised decks; Q2 spawns are sampled at the origin; convex occluders block light (the opposite of the visibility policy) | Contrast noise (1254 of 1396 findings across the hero sets) hides the real dark spots. | src/lighting.cpp:270-286; src/visibility.cpp:430 |
| **44.** Q2 DM/duel motif lights ignore lighting_style | 15 of Weft's 21 lights stay fixed whatever style is planned. | src/mp_motifs.cpp:77-85 |
| **45.** Q3 AAS validation counts global categories only | Spawns, items or flags can sit in areas bots cannot reach. AAS "valid 40/40" says nothing about individual entities. | cmake/RunQ3CompilerTest.cmake:205-275 |
| **46.** The engine smoke is hard-coded to baseq2 deathmatch, is not in CTest, and cannot be rebuilt from its documented root | No CTF, SP or co-op map has ever been engine-loaded, and initialization regressions go uncaught. | tools/map_recipes/engine_smoke.py:142, 151-154; CMakeLists.txt:229-390 |
| **47.** No runtime evidence includes a connected player | Spawn selection, pickup contact, movers, teleport arrival and rendering are all unproven. | tools/map_recipes/ENGINE_SMOKE.md:70-73 |
| **48.** The default compact Q2 DM/duel path has no compiler gate | A leak in default Q2 MP output would not fail CTest. | src/generator.cpp:223-226; cmake/RunQ2CompilerTest.cmake:141-143 |
| **49.** Rule cards are documentation only, with no schema and no reader | Lessons reach output only through hand-coding, and card evidence can drift silently. | knowledge/q2/rules.json |
| **50.** The reference corpus has no stock id maps, an unusable CTF cohort, and no usable navigation (all 454 AAS sidecars have zero reachabilities) | There is no q2dm1–8 baseline, and route and spacing rules are uncalibrated. | tools/map_study/corpus.py:134; out/map-study/summary.json |
| **51.** Evidence lives in the gitignored `out/`, `tools/` and `knowledge/` are untracked, and two CTest groups read `out/` | A clean clone cannot reproduce or audit the evidence behind the cards. | .gitignore:2, 4 |
| **52.** Version and schema do not track output changes, and prompt tests miss synonyms, contradictions and false positives | M3-era and current output cannot be told apart, and parser regressions go undetected. | src/report.cpp:595; tests/test_main.cpp:79-155 |
| **53.** Documented counts are stale | ROADMAP:55 says "15 rule hypotheses" against 60 cards, and the README counts disagree with each other. | docs/ROADMAP.md:55; docs/MAP_STUDY.md:106 |
| **54.** Latent: repair can add a stairless 64-unit platform | If that path fires it creates an unreachable ledge (64 > 40 jump-up). It is not reached in default output. | src/repair.cpp:369-386 |

## 4. Corrections to the brief's game facts

Every respawn time, spawnflag and classname in the brief was checked against the id source releases on this machine. The rows below list each discrepancy, plus the omissions that change a design rule. Values the brief states correctly are confirmed at the end of each game's table.

### Quake

| Game | Item/flag | Brief says | id source says | Source |
|---|---|---|---|---|
| Q1 | Mega Health, re-release | 20 s after it decays | A fixed 120 s from pickup. The rot moved onto the player. | quake-rerelease-qc-main/quakec/items.qc:208-221; client.qc:1400-1417 CheckHealthRot |
| Q1 | Mega Health, 1996 progs and QW | 20 s after it decays | Holds: a 5 s delay, then −1 HP/s down to max_health, then 20 s. With the 1996 progs only when `deathmatch == 1`. In QW deathmatch 4 it never respawns. | progs.dat health_touch stmts 1895-1909, item_megahealth_rot 1921-1947; qw-qc/items.qc:342-385 |
| Q1 | Weapon, armor, ammo, Mega gating, 1996 progs | weapons 30 s, armor 20 s | The values hold, but only when `deathmatch == 1` exactly. Under deathmatch 3+ these items never respawn. Weapons stay in deathmatch 2 and in coop. Weapon-stay for deathmatch 3/5 exists only in QW and the re-release. | progs.dat weapon_touch stmts 2129-2134, 2271-2283; armor_touch 1999-2005 |
| Q1 | Ammo respawn | (omitted) | 30 s (1996: only `deathmatch == 1`). 15 s in QW and re-release deathmatch 3/5. None in deathmatch 2. | progs.dat ammo_touch stmts 2488-2494; quakec/items.qc:741-749; qw-qc/items.qc:890-901 |
| Q1 | Biosuit | (omitted) | Respawns in 60 s and lasts 30 s. | progs.dat powerup_touch stmts 2970-2972; quakec/items.qc:1173-1174 |
| Q1 | Powerup gating | Quad 60 s, Pent/Ring 300 s | The values hold. 1996 and re-release: only when deathmatch ≠ 0. QW: unconditional. | qw-qc/items.qc:1330-1336; quakec/items.qc:1167-1176 |
| Q1 | Rotten health | (health 20 s) | 20 s holds. Rotten heals 15, not the QUAKED comment's "5–10". | progs.dat stmts 1799-1800; quakec/items.qc:154 |
| Q1 | Skill flags 256/512/1024, 2048 | 256/512/1024 not on easy/normal/hard; 2048 not in DM | The values hold. Skill flags are tested only when `deathmatch == 0` (SP and coop), and 2048 only when deathmatch ≠ 0. Nightmare uses the NOT_HARD set. QuakeWorld ignores skill flags entirely. | WinQuake/pr_edict.c:931-948 ED_LoadFromFile; QW/server/pr_edict.c:914-919 |
| Q1 | Intermission classname | (not stated; the Phase-1 S5 audit text used the Q2 name `info_player_intermission`) | `info_intermission`. FindIntermission falls back to `info_player_start`, then errors. | quakec/client.qc:135-168 FindIntermission |
| Q1 | Threewave CTF classnames | CTF via Threewave | `item_flag_team1/2` and `info_player_team1/2` exist only in quakec_ctf ("ThreeWave CTF 5.0"), not in id1. Team spawns serve both initial spawns and respawns and fall back to `info_player_deathmatch`. Runes need DM spots. | quakec_ctf/teamplay.qc:1198, 1217; quakec_ctf/client.qc:535-547, 695 |
| Q1 | CTF flag auto-return | (not stated) | `TEAM_CAPTURE_FLAG_RETURN_TIME` is 15, but the effective return is about 30 s. | quakec_ctf/teamplay.qc:914, 1091 |
| Q1 | Weapon list | complete weapon set | `weapon_supershotgun` is a valid classname; the repo omits it. | qw-qc/items.qc:703-706; src/map_builder.cpp:876-877 |
| Q1 | teamplay 1 self-damage | (rocket-jumping assumed) | 1996 progs and QW: no health loss from your own damage in teamplay 1, so rocket jumps cost armor only. The re-release applies self-damage. | quakec/combat.qc:217 |
| Q1 | DM spawn selection | (not stated) | 1996 progs: sequential, skipping spots with a player within 32 units. QW: random, 84 units. Re-release: random, 384 units and not visible, falling back to 84. | quakec/client.qc:537-687 |
| Q1 | Nightmare | "harder but not a random flood" | 1996: player keeps 100 HP, monsters lose the attack cooldown, Vore and Shambler get boosts. Re-release: SP player health 50, cooldown restored, boosts removed. | progs.dat SUB_AttackFinished stmts 249-256 |
| Q1 | Monster count | kill counts | The 1996 progs count fish twice in `total_monsters`; the re-release counts once. | progs.dat stmts 8606, 8682; quakec/monsters.qc:264 |

Confirmed as stated in the brief: weapons 30 s (outside the 1996 deathmatch-1 rule), armor and health 20 s, Quad 60 s, Pent/Ring 300 s, func_door GOLD_KEY 8 / SILVER_KEY 16 (`item_key2` gold, `item_key1` silver), jump +270, `sv_maxspeed` 320, gravity 800.

### Quake II

| Game | Item/flag | Brief says | id source says | Source |
|---|---|---|---|---|
| Q2 | NOT_COOP 4096 | "Q2 adds 4096 = not in co-op" | Defined, but its test is commented out and the flag is only cleared, so it has no effect in 3.19. No shipped SP map uses it. 4096 is also DF_ALLOW_EXIT in the separate dmflags namespace. | game/g_local.h:54; game/g_spawn.c:587, 599; q_shared.h:1028 |
| Q2 | Skill flags in DM | 256/512/1024/2048 | The values hold. In DM only NOT_DEATHMATCH is tested. Skill 3 uses the NOT_HARD set. | game/g_spawn.c:574-599 |
| Q2 | Power Screen / Power Shield | armor 20 s | 60 s. Jacket, combat, body and shard are 20 s, as the brief says. | game/g_items.c:681, 738, 1251, 1274 |
| Q2 | Mega Health | 20 s after it decays | Holds, but decay starts 5 s after pickup at 1 HP/s, so the respawn is 5 s + decay + 20 s. | game/g_items.c:541-552, 570-578 |
| Q2 | Health, ammo, other pickups | (omitted) | Health 30 s; ammo 30 s; silencer, rebreather, enviro suit, adrenaline, ancient head and bandolier 60 s; ammo pack 180 s. | game/g_items.c:582, 508-509, 197-332 |
| Q2 | Item spawnflags | (not stated) | SpawnItem zeroes non-skill spawnflags on placed items (except key_power_cube), so TRIGGER_SPAWN and NO_TOUCH do nothing. | game/g_items.c:1065-1072 |
| Q2 | Cross-level state | `trigger_crosslevel_trigger` (fact-task wording) | The classnames are `target_crosslevel_trigger` / `target_crosslevel_target`. | game/g_spawn.c:197; game/g_target.c:448-486 |
| Q2 | Key locks | `trigger_unlock` (fact-task wording) | No such entity exists. `trigger_key` with an `item` key relays to a targeted `func_door`, which has no key field. | game/g_trigger.c:208-307 |
| Q2 | target_help spawnflag 1 | field-computer primary objective | A mapping convention only: the code writes helpmessage1 or helpmessage2. Shipped maps follow the convention. | game/g_target.c:118-146 |
| Q2 | Teleporters | teleporters | The base game has `misc_teleporter` (a point pad with a 16-unit trigger) plus `misc_teleporter_dest`. The brush `trigger_teleport` exists only in the CTF DLL. | game/g_misc.c:1827-1861; ctf/g_spawn.c |
| Q2 | CTF team spawns | official CTF | `info_player_team1/2` are used only for a player's first spawn after joining a team; later respawns use `info_player_deathmatch`. | ctf/g_ctf.c:449-455 |
| Q2 | CTF flag return | (not stated) | `CTF_FLAG_RETURN_TIME` 40 is defined but unused; the effective auto-return is 30 s. | ctf/g_ctf.h:84; ctf/g_ctf.c:870 |
| Q2 | Mover defaults | (not stated) | The QUAKED comments disagree with the code: func_train dmg is 100 (comment says 2), and func_plat speed is 200 (comments say 150 and 200). | game/g_func.c:496-526, 1663-1664 |
| Q2 | Air acceleration | (strafe-jumping assumed) | `pm_airaccelerate` only toggles air control on or off (it then uses `pm_accelerate` 10). `sv_airaccelerate` is forced to 0 outside DM. | qcommon/pmove.c:654-655; server/sv_init.c:196-205 |
| Q2 | Version | "Quake II" | The local source is 3.19, not the 3.20/3.21 retail binaries. | qcommon/qcommon.h:26 |

Confirmed as stated in the brief: weapons 30 s (DF_WEAPONS_STAY keeps them), armor 20 s, Quad 60 s, Invulnerability 300 s, spawnflag values 256/512/1024/2048, jump 270, gravity 800, `pm_maxspeed` 300.

### Quake III Arena

| Game | Item/flag | Brief says | id source says | Source |
|---|---|---|---|---|
| Q3 | Weapon respawn in team modes | 5 s (30 s in team modes) | 30 s only when `g_gametype == GT_TEAM`. CTF and the other modes use 5 s. | code/game/g_items.c:268-269; g_main.c:146-147 |
| Q3 | Powerups | 120 s | 120 s after a pickup. At map start powerups are hidden for 30–60 s. | code/game/g_items.c:44, 693-701 (696) |
| Q3 | Ammo, holdables | (omitted) | 40 s and 60 s. | code/game/g_items.c:41-42 |
| Q3 | Mega Health | 35 s | 35 s holds (the source keeps a commented-out 120). Per-entity `wait` and `random` override any timer, and several stock maps set wait 120 or 45. | code/game/g_items.c:43, 506-525, 881-882 |
| Q3 | `gametype` entity key | (treated as Team Arena only) | Present in the baseq3 1.32b build, using substring matching. | code/game/g_spawn.c:412, 458-468 |
| Q3 | Tourney token | "tourney" | The entity key expects "tournament"; arena files and the UI use "tourney". | code/game/g_spawn.c:412; code/q3_ui/ui_startserver.c:136 |
| Q3 | `notfree` | (not stated) | Removes entities in FFA, Tournament and Single Player alike (every gametype below GT_TEAM). | code/game/g_spawn.c:436-441 |
| Q3 | Health classname | "health/MH" | `item_health`; there is no `item_health_normal`. | code/game/bg_misc.c:137 |
| Q3 | `info_player_deathmatch` spawnflag 1 | (initial spawn) | Used only for a local client's first spawn; remote players and bots ignore it. | code/game/g_client.c:281-299, 1085-1087 |
| Q3 | Jump pads | trigger_push + target_position, predictable arcs | The game aims with AimAtTarget, but the bot model uses 1.1× the horizontal speed, so the bot's arc differs from the real one. | code/game/g_trigger.c:160-192 (188); code/botlib/be_aas_reach.c:279 |
| Q3 | `info_camp` | info_camp where needed | Kept in game despite its QUAKED comment. Range, weight, wait and random are parsed but never used. | code/game/g_misc.c:33-38; code/botlib/be_ai_goal.c:508-511 |
| Q3 | `item_botroam` | item_botroam where needed | Needs a `weight` key. Without one its weight is 0 and bots never choose it. | code/botlib/be_ai_goal.c:1360-1362 |
| Q3 | `notbot` | notbot where needed | A botlib-only item flag: the item still spawns for players, and BSPC ignores it. | code/botlib/be_ai_goal.c:630-631 |
| Q3 | botclip | "simple clip around detail" | q3map supports `surfaceparm botclip`, but no `common/botclip` shader ships. | q3map/shaders.c:87; Q3A scripts/common.shader |
| Q3 | BSPC physics | (not stated) | cfgq3.c sets maxstep 18 and airaccelerate 0, while botlib defaults without `-cfg` are 19 and 1. | code/botlib/be_aas_move.c:89-91; code/bspc/cfgq3.c |

Confirmed as stated in the brief: armor 25 s, health and Mega 35 s, weapons 5 s outside GT_TEAM, armor classnames shard/combat/body, `pm_airaccelerate` 1, `g_speed` 320, jump 270, gravity 800.

### Runtime contracts behind these facts, and what is unavailable

- **Quake.** The shipped id1 `progs.dat` (1996, v6, CRC 5927, sha256 `f2619787…`) was disassembled read-only, so its facts are cited by function and statement index, not file:line. Whether it is v1.01 or v1.06 could not be determined. Other sources: QuakeWorld qw-qc, the 2021 re-release QuakeC, and its `quakec_ctf` ("ThreeWave CTF 5.0"); the WinQuake and QW engines for spawnflag filtering and physics. KTX was used only to corroborate. **Not available locally:** the original 1996 QuakeC source text, the re-release (KEX) engine source, and any Threewave 4.x QuakeC. Ambush-flag handling was not traced.
- **Quake II.** id 3.19 source: the `game/` baseq2 DLL, the `ctf/` DLL ("ThreeWave CTF 1.09b"), pmove and server. Shipped BSP entity lumps were used to cross-check map conventions. **Not verified:** the 3.20/3.21 retail behaviour and the re-release game DLL (its source is local but the facts task did not examine it). The help-screen image was not inspected.
- **Quake III Arena.** id 1.32b source (game, cgame, botlib, bspc, q3_ui, q3map) plus the shipped baseq3 shaders, arenas, botfiles and 36 BSP entity lumps. **Not available:** point releases before 1.32b. BSPC was later run on 40 generated maps by the benchmark, but none contains a jump pad, so the jump-pad reachability path is still unexercised.
- **All three games.** No fact was measured at runtime. Derived numbers (Q1 jump height of about 45.6 units, maximum rocket-jump knockback, the Q2 fall-damage threshold) are arithmetic from code.

## 5. Draft benchmark

### Design

- **Manifest:** `tools/benchmark/draft-v0.json` (schema 1, sha256 `056889a0…`).
  - 17 targets × 8 prompts (3 vague, 3 specific, 2 contradictory), plus 2 Q2 showcase probes (the hero prompt and "a Quake II deathmatch map").
  - Targets: Q1 sp, coop, dm, duel, tdm, ctf-threewave; Q2 sp, coop, dm, duel, tdm, ctf; Q3 botarena, ffa, tourney, tdm, ctf.
  - Every case lists its fixed seed and the explicit requests a correct brief must honour (859 in total). These requests were not tuned to the current parser.
- **Seeds:** consecutive per target, from 1000 to 1135. Both showcase probes use 4242, so there are 137 distinct seeds.
- **Run:** prompts go through the normal CLI only (`qmg generate --prompt … --seed …`), never with `--game` or `--mode`. Planner: rules/v1 (no model passed).
- **Compile flags** (tool pins checked before compiling; 600 s cap per stage):
  - Q1: `qbsp -leaktest -noallowupgrade -notex`, then `vis -threads 2` (full; no `-fast`), then `light -extra4 -bounce 1 -soft -threads 2 -nodefaultpaths`. This matches the repo gate, including the absence of a texture WAD.
  - Q2: `qbsp -q2bsp -leaktest -noallowupgrade -nodefaultpaths -path <baseq2>`, then the same VIS/light with the same `-path`. Tools: ericw-tools 2.0.0-alpha11.
  - Q3: `q3map2 -bsp -leaktest`, then `-vis -saveprt`, then `-light -samples 2 -filter -bounce 1 -patchshadows`, with `-fs_basepath`/`-fs_game` pointed at the stock assets and a private `-fs_homepath`; then `mbspc -bsp2aas` and `-aasinfo`.

| Gate | Passes when |
|---|---|
| G1 generated | qmg exits 0, the `.map` is not empty, and the report parses |
| G2 intent | resolved game and mode equal the target's. sp → single_player; dm, ffa → deathmatch; duel, tourney → duel; ctf → capture_the_flag. coop, tdm, botarena and ctf-threewave are not representable and always fail. |
| G3 compile | BSP stage exits 0, no `.pts`/`.lin` or leak message, no invalid-brush diagnostics |
| G4 full VIS | `.prt` and the visdata lump are not empty; for Q1/Q2, the log also has "Calculating Full Vis" |
| G5 final light | the lightdata lump is not empty |
| G6 AAS (Q3) | EAAS magic, version 5, areas == settings, reachability totals consistent |
| G7 mode entities | sp: `info_player_start` + exit (Q2: `trigger_once` linked to `target_changelevel`); coop: `info_player_coop`; DM family: ≥ 2 `info_player_deathmatch`; ctf: both flags and both teams' spawn classnames; tdm: n/a (no stock game has TDM spawn classnames) |

### Results

| Target | Cases | Generated | Intent | Compiled | Full VIS | Final light | AAS | Mode entities | All gates |
|---|---|---|---|---|---|---|---|---|---|
| q1-sp | 8 | 8 | 8 | 8 | 8 | 8 | n/a | 8 | 8 |
| q1-coop | 8 | 8 | 0 | 8 | 8 | 8 | n/a | 0 | 0 |
| q1-dm | 8 | 8 | 7 | 8 | 8 | 8 | n/a | 7 | 7 |
| q1-duel | 8 | 8 | 7 | 8 | 8 | 8 | n/a | 7 | 7 |
| q1-tdm | 8 | 8 | 0 | 8 | 8 | 8 | n/a | 0 (8 n/a) | 0 |
| q1-ctf-threewave | 8 | 8 | 0 | 8 | 8 | 8 | n/a | 0 | 0 |
| q2-sp | 8 | 8 | 7 | 8 | 8 | 8 | n/a | 7 | 7 |
| q2-coop | 8 | 8 | 0 | 8 | 8 | 8 | n/a | 0 | 0 |
| q2-dm | 8 | 8 | 8 | 8 | 8 | 8 | n/a | 8 | 8 |
| q2-duel | 8 | 8 | 7 | 8 | 8 | 8 | n/a | 7 | 7 |
| q2-tdm | 8 | 8 | 0 | 8 | 8 | 7 | n/a | 0 (8 n/a) | 0 |
| q2-ctf | 8 | 8 | 7 | 8 | 8 | 8 | n/a | 7 | 7 |
| q3-botarena | 8 | 8 | 0 | 8 | 8 | 8 | 8 | 8 | 0 |
| q3-ffa | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
| q3-tourney | 8 | 8 | 2 | 8 | 8 | 8 | 8 | 8 | 2 |
| q3-tdm | 8 | 8 | 0 | 8 | 8 | 8 | 8 | 0 (8 n/a) | 0 |
| q3-ctf | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
| q2-showcase | 2 | 2 | 1 | 2 | 2 | 2 | n/a | 2 | 1 |
| **Total** | **138** | **138** | **70** | **138** | **138** | **137** | **40/40** | **85 pass, 29 fail, 24 n/a** | **70** |

Intent matches by game: Q1 22/48, Q2 29/48 plus showcase 1/2, Q3 18/40.

### Failure classes

| Class | Cases | Gates failed | Cause |
|---|---|---|---|
| Target mode not representable | 56 | G2 in all; G7 fails for co-op (16) and Threewave (8), is n/a for TDM (24), and passes for bot arena (8) | No GameMode for co-op, TDM or bot arena. Threewave is not representable by policy: the generator does emit quake1/capture_the_flag. Resolved as: co-op SP 15, DM 1; TDM DM 15, SP 9; bot arena DM 7, SP 1; Threewave CTF 8. |
| Vocabulary miss on a representable target | 12 | G2 in all; G7 in 5 | "LAN party", "two players", "one-on-one" and "q2ctf" → SP (4). "tourney"/"tournament" → DM (5) or SP (1). "single player deathmatch" → DM (q2-sp-c1). "1v1" outranks "deathmatch" (hero → duel). |
| Compiler stage cap | 1 | G5 | q2-tdm-c2 (32 rooms, 126 lights): light hit the 600 s cap. A solo rerun took 790 s and exited 0 with 0 empty lightmaps. |

### Key observations

- **Mode parsing** (src/prompt.cpp parse_mode:204-221, parse:249-252) accounts for every G2 failure on representable targets. Q3 FFA passes 8/8, but only 3 prompts match "free for all". The other 5 rely on the silent Q3 deathmatch default. Q3 "campaign" prompts report single_player while the map is a DM arena with no warning.
- **Contradictions are handled silently.** 0 of 34 contradictory cases produced a conflict diagnostic, and all exited 0 with validation ok. Examples: a duel for 16 players is forced to 2 players; "32 players in 2 rooms" gives 2 rooms; "flat … extreme verticality" gives verticality 15. Ten contradictory cases name two modes, so one of them is always not honoured.
- **Requests across all 859:**

  | Category | Honoured | Partial | Not honoured | Not representable | Unverified |
  |---|---|---|---|---|---|
  | vague (148) | 105 | 0 | 17 | 21 | 5 |
  | specific (545) | 243 | 34 | 188 | 21 | 59 |
  | contradictory (166) | 71 | 2 | 54 | 14 | 25 |
  | **all (859)** | **419** | **36** | **259** | **56** | **89** |

  The hero prompt (15 requests): 1 honoured, 8 not honoured, 6 unverified. It became a 6-room duel with 2 spawns, the industrial theme, no railgun, Quad, Body Armor, Mega Health or sky, and every room floor at z = 0.
- **What no map contains.** None of the 138 maps has a sky or liquid texture, `func_plat`, `func_door`, teleporter, `trigger_push`, Quad, Mega Health, key or secret. Major-item requests: 47/53 not honoured. Named monsters outside the fixed roster are never placed.
- **Verticality.** Room-source floors are [0] in 138/138 maps. The per-map maximum walkable height is 0 in 5 maps, 48 in 4, 64 in 28, 80 in 79, 128 in 21 and 192 in 1. All 18 floor-level requests were not honoured. Plan verticality is 50 in 126/138, lighting_style is balanced in 138/138, and the planner is rules/v1 in 138/138.
- **Themes** use the first match in table order, so any prompt containing "water" becomes waterworks, including one that says "no water". "Strogg base" becomes techbase and "Strogg refinery" becomes industrial. The 41 theme requests counted as honoured compare only the report label, not materials.
- **Texture resolution.** Q1 WALL1_1 and FLOOR0_1 appear in none of the 51 id1 BSPs (CEIL1_1 and trigger do). All 4 Q2 names have WALs in baseq2, and all 4 Q3 names resolve to a shader or image. Not exercised: the Q2 liquid WALs and the Q3 qmg/* liquid shaders do not exist.
- **AAS** in 40/40 maps (areas 17–332): walk reachability in 40, barrier jump and walk-off-ledge in 39, rocket jump in 28 (BSPC inference over plain boxes). There are **0 jump-pad, teleport or elevator reachabilities** anywhere.
- **Variety.** Every target has 8/8 distinct layout hashes and 6–8 distinct room-graph signatures. Vague prompts always give 6 rooms. The hero and vague showcase maps share a room-graph signature.
- **Timings** (wall time under 8 parallel jobs × 2 threads, plus other machine load):
  - generate: median 0.18 s, max 8.5 s
  - Q1/Q2 qbsp and VIS: ≤ 1.5 s each
  - Q1+Q2 light: median 12.9 s; Q2 light 165–330 s for 5–10 room maps with many lights; the 32-room q2-tdm-c2 needs 790 s solo
  - q3map2 light: median 11.6 s, max 77.7 s (32-room CTF)
  - BSPC: ≤ 1 s
- **Advisory analyzers report serious findings on maps that pass every gate:** 11 unlit_critical_placement, 116 unreachable_monster, 240 unreachable_timing_target and 88 player unbound_placement. None of these is a gate.

### Independent check

An independent BSP/AAS/.map parser was run over all 138 cases, and the current `run.py` functions were re-run against every report and map. Every per-target and total count was reproduced, with 0 gate differences and 0 request-status differences. Leak detection was confirmed by compiling a deliberately leaky map with the runner's exact flags: Q1/Q2 qbsp exit 1 and write `.pts`; q3map2 exits 0 but writes `.lin` and no BSP. All three are caught. The visdata and lightdata lumps were verified empty before their stages ran, and no stage used `-fast`. Corrections from the check, applied above: Q3 FFA has 3/8 explicit matches, not 0; the height distribution is as listed; and the 243/34/188/21/59 request totals cover the specific category only.

### Policy caveats

- **Threewave.** Q1 Threewave counts as not representable, and its G7 requires team spawns, although the Threewave QC falls back to DM spawns (quakec_ctf/client.qc:535-547). Under the alternative policy, Q1 CTF would pass 8/8. The v1/v3 prompts never mention Threewave.
- **Contradictory prompts.** G2 on two-mode prompts reflects parser precedence. 17 of the 70 full passes are silently resolved contradictions (53/138 if surfacing were gated).
- **Missing gates.** G7 checks classname presence only. No gate covers analysis errors, texture existence, determinism, request honouring or runtime.
- **The 600 s light cap** fails a 32-room Q2 map that completes in 790 s. RC1 needs a measured budget.
- **Provenance.** Three smoke-case results were written by a pre-edit `run.py`; they re-evaluate identically. RUNNER_VERSION stayed at 0.1.0, so results do not identify the runner code.
- **Vague prompts.** Some are open to interpretation ("something fun for a Quake LAN party" was expected to be DM).

### Limits

- **Requests.** Request evaluation is mechanical (classname and texture tables, report fields, floor heights). Placement qualifiers (backside, exposed, far apart), mood and symmetry are never verified. Player counts are always unverified because the report has no players field.
- **Textures.** Q1 was compiled without textures. Q1 texture resolution checks only the miptex embedded in stock BSPs. Q2 resolution checks loose WALs in a partial baseq2 with no pak. Sky and liquid detection by texture-name substring is a latent false-negative risk for Q3.
- **Variety.** Room-graph variety uses a home-grown WL hash, not a canonical graph export.
- **Status.** The benchmark is not in CTest. Its outputs live in the gitignored `out/benchmark-draft/`, and the runner lives in the untracked `tools/` tree.

**Evidence level of this section:** generation, static analysis of reports and maps, and compiler output (BSP, VIS, light, AAS). No engine, client, bot or human evidence.

## 6. Area scores

### Rubric

| Score | Meaning |
|---|---|
| 0 | No code path serves the claimed game × mode for this area. |
| 1 | Placeholder. The output compiles but is fixed and not game- or mode-aware in substance, or the capability exists only in recipes, tests or unreachable code. |
| 2 | A real implementation that is mode-aware, prompt-controllable, tested and compiled, for at least one claimed mode of the game, but narrow. |
| 3 | At least one game × mode family works at professional-blockout depth (typed, tested, compiled, with evidence above static analysis), and the other claimed paths sit at 0–2. |
| 4 | Most claimed game × mode paths at that depth, with runtime evidence in each game. |
| 5 | Every claimed game × mode meets the area's release-candidate bar, gated in the benchmark, with the evidence level the RC definition asks for. |

How the rubric was applied:
- The overall score is a judgement across all 17 paths, not an average of the per-game scores.
- When the evidence is recipe-only or covers a single game × mode, the lower score is chosen.
- Every score was challenged once, and none changed.

| Area | Overall | Q1 | Q2 | Q3 | Verdict |
|---|---|---|---|---|---|
| A Prompt → design brief | 2 | 2 | 2 | 2 | Typed, hardened intake that resolves SP/DM/duel/CTF, but TDM, co-op, bot arena and tourney are absent, contradictions and unsupported requests are dropped silently, and even Q2 DM cannot hold the hero prompt. |
| B Classic-map study | 3 | 0 | 3 | 1 | Q2 DM/duel is studied in depth (460 maps, tested splits, compiler and native evidence). Q1 has nothing, Q3 has only a QL atlas reader built for Q2 design, there is no stock calibration, and nothing reads the cards. |
| C Layout and construction | 2 | 1 | 2 | 1 | Only Q2 DM/duel has a real motif vocabulary, single-plane and with no mechanisms. The other 15 paths share a game-blind ring or chain. |
| D Items, spawns, economy | 2 | 1 | 2 | 1 | Spawn refinement and station binding are real only for compact Q2 DM/duel. The item economy is a round-robin placeholder in every game. |
| E Single-player | 2 | 2 | 2 | 1 | Q1/Q2 SP maps are real and compile, but are fixed-slot monster chains with no skill flags, keys, secrets, ammo or co-op. Q3 bot arena is relabelled FFA. |
| F Theme and materials | 1 | 1 | 1 | 1 | One fixed palette per game that ignores theme. Q1 textures are missing, Q2 has lamp-tile ceilings and near-black walls, Q3 walls are near-black, and there is no sky or working liquid. |
| G Lighting | 2 | 1 | 2 | 1 | One point light per room, plus Q2 DM/duel stair and passage lights. Compiled Q1/Q3 floors are near-black and Q2 is a flat orange; the motif lights help only in static analysis. |
| H Validation and evidence | 3 | 2 | 3 | 2 | Engine-initialization and native-movement depth exist only for Q2 DM/duel, partly on older or recipe BSPs. Q1/Q3 stop at compiler output. There is no per-entity bot reachability and no playtest protocol run. |

### A — Prompt → design brief (2; Q1 2, Q2 2, Q3 2)

**Evidence:**
- `PromptSpec` has 12 fields and nothing records whether a field was explicit or defaulted (types.hpp:48-61).
- `GameMode` has four values (types.hpp:16-21).
- `parse_mode` uses fixed precedence with substring and word matching (prompt.cpp:204-221).
- In the benchmark, rules/v1 set lighting_style to balanced in 138/138 maps and verticality to 50 in 126/138.
- The only negation and conflict handling is the fail-closed wrapped_center grammar, which is tested (prompt.cpp:58-107; tests/test_wrapped_intent.cpp:48-69).
- Q3 has the only real-GGUF CTest, one opt-in duel prompt, which is model-inference evidence but narrow.
- All of this is static, plus compiler output for the 70 intent matches.

**Top gaps:**
1. Claimed modes are missing: TDM in all three games, co-op in Q1/Q2, Q3 bot arena. Synonyms such as tourney/tournament are not recognised, and there is no game-module field (56 cases not representable, plus 6/8 tourney misses).
2. Unsupported requests are dropped silently and defaults are unmarked: 259 not-honoured and 89 unverified requests with zero feedback.
3. There is no content vocabulary for items with placement intent, landmarks, liquids, sky, mechanisms, difficulty, keys, secrets or units. Building one requires semantic-plan-v2.

### B — Classic-map study (3; Q1 0, Q2 3, Q3 1)

**Evidence:**
- Q2 is well covered:
  - 460 BSPs analysed with 0 quarantined, split 299 reference / 117 validation / 37 test / 7 study_only (static).
  - Cohorts: duel 130 maps, dm_small 112, dm_medium 142, dm_large 13; CTF has only 3 maps and is unusable.
  - Provenance and partitioning are union-found and tested (tests/test_map_study.py:199-240).
  - 10/10 generated cases compiled and band-compared (compiler). That comparison is stale: it used `-notex`, no VIS/light, and predates the compact generator.
  - 131 native trial records exist.
- The reader accepts only IBSP38 (binary.py:178-181).
- `knowledge/` has only `mp/` and `q2/`, and nothing in `src/` or `include/` reads it.
- No stock id map is in the corpus. Under the current provenance rules stock maps would become study_only and be excluded from profiles (static inference, not executed).

**Top gaps:**
1. No Q1 study path: no BSP29/BSP2 reader, no knowledge/q1, no DM3/DM2/DM4/DM6 or E1M1/E1M7 rule.
2. No Q3 study path: the reader is a QL render atlas, the AAS graph drops jump-pad travel, and there is no baseq3 calibration or pro- comparison.
3. No single-player knowledge in any game: the extractor filters DM spawnflags only, and stock SP maps sit unused.

### C — Layout and construction (2; Q1 1, Q2 2, Q3 1)

**Evidence:**
- 23/138 benchmark maps (resolved Q2 DM 15, duel 8) used the compact path (generator.cpp:223-226).
- Legacy MP geometry hashes identically across Q1 DM, Q3 DM and Q3 duel at a fixed seed (static).
- Room-source floors are [0] in 138/138 maps.
- Maximum walkable height (compiler-level report): Q2 DM 128, Q2 duel 128 or 192, every legacy path 48–80.
- Across 138 maps the only mechanism-like classnames are the SP exits.
- Recipe mechanisms carry native and engine evidence but cannot be reached from `qmg generate`, so they earn no credit.

**Top gaps:**
1. Q1/Q3 MP and Q2 CTF have no motif vocabulary (11 of 17 targets get a loop of plain boxes).
2. The default planner cannot select any mechanism (0 movers, teleporters or liquids in 138/138).
3. Verticality is single-plane: no multi-floor rooms or over/under routes, and 0/18 floor-level requests honoured.

### D — Items, spawns and economy (2; Q1 1, Q2 2, Q3 1)

**Evidence:**
- Weapon tables are incomplete and placement is index-driven (map_builder.cpp:876-883; entities.cpp:214-219).
- Health and armor have one class each (map_builder.cpp:933-944).
- No respawn timing exists anywhere in `src/`.
- Entity tallies over compiled maps:
  - Q2 railgun absent from 22/30 arena maps, and 3 Q2 maps have no RL.
  - Q3 plasmagun absent from 30/40.
  - Q3 has 3 or more Red Armors in 35/40 maps.
  - Q1 has 3 or more Yellow Armors in 22/26 arena maps, and 23/26 repeat a weapon.
- Analyser-safe spawns (static): Q2 duel 16/16, Q2 DM 55/108, Q1 DM 20/76, Q3 DM 56/158, Q3 CTF 25/82, Q2 CTF 10/140.
- 47/53 major-item requests were not honoured.
- 14 Q2 DM BSPs pass dedicated engine initialization, including a buried-item negative control (engine).

**Top gaps:**
1. Spawn and intermission entities that break or telefrag at runtime: no Q1 intermission, no Q1/Q2 MP start, no co-op starts, a single Q3 CTF team-begin spot per team (static reading, not engine-tested).
2. No item economy: no ammo, Mega, armor tiers, powerups or holdables; missing weapons; no respawn data.
3. Placement ignores item role, so there is no power side, backside or control trade-off.

### E — Single-player (2; Q1 2, Q2 2, Q3 1)

**Evidence:**
- `populate_single_player` is a real, game-aware path with SP validator checks (entities.cpp:244-337; validator.cpp:419-443).
- The `qmg_q1_sp6` and `qmg_q2_sp6` fixtures compile.
- In the benchmark, q1-sp passes 8/8 and q2-sp 7/8 (compiler). G7 checks only a start and an exit, so these passes say nothing about encounters or progression.
- Spawnflags are empty in 30/30 SP-resolved maps.
- Requests: keys 0/5, secrets 0/6, coop spawns 0/11, liquids 0/10.
- The 8-room Q1 SP map ships 29 navigation errors while the CLI prints "validation=passed".
- Q3 "SP" is the FFA arena population (entities.cpp:347).
- No SP map has been engine-loaded: the smoke hard-codes deathmatch 1.

**Top gaps:**
1. Levels are not provisioned to be beaten: no ammo, no armor, Q2 health is 10 HP, and weapon order is not monotonic. An 8-room Q1 probe holds about 2785 monster HP against 25 shells plus pickup grants.
2. There is no progression structure: no keys, doors, secrets or objectives, and the loop shortcut reaches the exit in about 4.5 s.
3. Co-op is absent in Q1 and Q2 (16/16 co-op cases fail G7).

### F — Theme and materials (1; Q1 1, Q2 1, Q3 1)

**Evidence:**
- Materials are a pure function of (game, role, contents) (map_builder.cpp:242-368).
- All 138 maps use exactly 4 names per game across 11 theme labels.
- Q1: WALL1_1 and FLOOR0_1 cover 34,597 of 35,448 faces (97.6%) and are not stock names. qbsp reported "No wad" and "unable to find texture" 48/48 times (compiler).
- Q2: the ceil1_4 ceilings are SURF_LIGHT 10000 emitters (compiler, reproduced), and the flat1_1 walls are near-black (mean RGB about 18, 14, 10).
- Q3: caulk covers 71% of faces, which is correct practice, but the comp3text walls are near-black blend-layer images.
- The Q2 recipe palette (sky, accents) is never reached from `qmg generate`.

**Top gaps:**
1. Q1 output is untextured: no stock names, no `wad` key, no WAD option, and this is hidden by `-notex`.
2. The Q2 and Q3 default palettes use unsuitable stock assets: lamp-tile ceilings and near-black walls.
3. There are no theme-driven per-game kits and no role coverage for trim, accent, light, hazard, sky or clip.

### G — Lighting (2; Q1 1, Q2 2, Q3 1)

**Evidence:**
- One light per room (entities.cpp:154-175), plus stair and passage lights on compact Q2 DM/duel only (mp_motifs.cpp:77-85, 540-550).
- In 138 compiles there are 0 suns, 0 sky faces and 0 switchable styles (compiler logs).
- A compiled floor-lightmap probe over 137 lit BSPs, calibrated on stock maps with the same parser (compiler; no rendering, no engine gamma):

  | Game | Generated floors | Stock maps |
  |---|---|---|
  | Q1 | median floor mean 6.6/255; 89–99% of floor luxels below 16 | floor mean 46–95 |
  | Q3 (stored bytes) | 12.4–25.9; median 65% below 16 | 48–110 |
  | Q2 | uniform 174.0–174.9 at (255, 163, 41) | 45–73 |

- The analyzer calls Q1 92.6% readable, which contradicts the compiled output (static vs compiler).
- The benchmark has 11 unlit_critical_placement errors.

**Top gaps:**
1. Compiled maps are not readable in any game, and light placement is not calibrated to each compiler's falloff.
2. Q2 surface lights are unmanaged and dominate every Q2 map.
3. There are no visible light sources: no fixtures, torches, strips or deliberate light textures.

### H — Validation and evidence (3; Q1 2, Q2 3, Q3 2)

**Evidence:**
- 64/64 CTest groups pass with every optional gate configured.
- Compiler fixtures exist for all three games, each with a live negative leak control.
- The Q2 dedicated smoke has 16 records (`ok`, 16 checks each): 14 study maps plus a Spillway positive control and a buried-item negative control. Of the 14, six are default-generator output (Weft, Splice, Lattice, Braid, Hinge, Span) that predate the current build, and eight are recipe maps (engine).
- 131 native trial records exist (native).
- Gate weaknesses: `-notex`, `--no-report`, weak completion markers, and no gate on the compact Q2 DM/duel path.
- The benchmark is not a release gate.
- Q3 AAS validation is global only: the ctf8 fixture passes with a flag the analyzer reports unreachable.
- Q1 has no engine, Pmove or bot run of any kind.

**Top gaps:**
1. No Q1 or Q3 engine-initialization smoke (RC4 unmet for 2 of 3 games).
2. Q3 bot reachability is checked only through global AAS counts, with no per-entity binding.
3. The project's own analysis errors never gate output.

## 7. Release-candidate path readiness

RC1 = benchmark of ≥ 8 prompts passes every hard gate; RC2 = depth parity with today's Q2 DM; RC3 = hero chosen in advance, with study, viewer and package; RC4 = engine smoke passes (and Q3 bots reach every item); RC5 = README/ROADMAP state the gaps and playtest status.

| Path | Runtime contract | RC1 | RC2 | RC3 | RC4 | RC5 | Feasibility | Recommendation |
|---|---|---|---|---|---|---|---|---|
| Q1 SP | NetQuake SP. Choice of 1996 id1 progs (no source text) or 2021 re-release QC (needs KEX-style localization). | 8/8 at compiler level; textures masked; 29 nav errors ungated | Far below: no keys, doors, secrets, skill flags, ammo, armor or intermission | None | Not met: no Q1 smoke or harness, no reviewable pinned engine | Not met | Low | Decide later |
| Q1 co-op | coop 1 (forces deathmatch 0); cycles `info_player_coop`, falls back to start | 0/8: not representable; no `info_player_coop` | Absent; inherits SP gaps | None | Not met; would need two clients | Not met; co-op never mentioned | Low | Decide later, with Q1 SP |
| Q1 DM | deathmatch 1; item gating and spawn selection differ between 1996 and re-release | 7/8 ("LAN party" → SP); texture caveat | Far below: legacy ring identical to Q3 DM; no SSG, ammo, MH or powerups; no intermission (fatal at match end) | None (dm1–dm6 local for calibration) | Not met: fteqw-sv buildable from source; ironwail binary has no local source to review | Not met | Medium | Keep if inputs supplied |
| Q1 duel | deathmatch 1 with two players; id1 has no duel mode | 7/8 ("two players" → SP) | Far below: 2 spawns, no item-control design, plus all Q1 DM gaps | None | Not met; a Q1 DM smoke would cover it | Not met | Medium | Keep if inputs supplied |
| Q1 TDM | teamplay 1/2 on DM; no team entities; self-damage rule differs by progs | 0/8 (DM 4, SP 4) | Absent | None | Not met | Not met; TDM never mentioned | Low | Decide later |
| Q1 CTF (Threewave) | Only re-release `quakec_ctf` ("Threewave CTF 5.0") is local; no 4.x, no compiled progs, no QC compiler built, KEX unavailable | 0/8 by policy (8/8 under the alternative); no team starts | Far below: unmirrored, weapon in each flag room, no intermission | None | Not met; no runnable module locally | Not met; Threewave never mentioned | Low | Likely drop |
| Q2 SP | 3.19 baseq2 (re-release DLL not examined) | 7/8 ("single player deathmatch" → DM, silent) | Far below: no unit/hub, `trigger_key` locks, objectives, secrets, skill flags, ammo or armor | None | Not met: smoke hard-codes DM and cannot be rebuilt from its documented root. A dedicated server forces deathmatch 1 unless coop is set (server/sv_init.c:318-324), which frees monsters, so SP maps can be initialized only under coop 1 or with a client | Not met | Low | Decide later |
| Q2 co-op | coop 1; `info_player_coop` matched by spawnpoint; NOT_COOP inert in 3.19 | 0/8 (SP 7, DM 1) | Absent; inherits SP gaps | None | Not met | Not met; co-op never mentioned | Low | Decide later, with Q2 SP |
| Q2 DM | 3.19 baseq2 deathmatch 1; Q2REPRO aeeb335f dedicated + pinned legacy game DLL | 8/8; hero prompt fails G2 (→ duel), 8/15 requests not honoured | The reference path, itself thin: 2 families, no mechanisms, round-robin items, ceil1_4 emitters | Designated in the brief; not deliverable yet | Partly: dedicated init on 14 studies + 2 controls; 131 native trials; no client; pins only in gitignored `.agents/tmp`; not in CTest | Not met: playtest status unstated; stale counts | High | Keep |
| Q2 duel | deathmatch 1 with two players; 3.19 has no duel mode | 7/8 ("one-on-one" → SP) | Closest to parity (shares compact path); 2 spawns, combat armor only, no MH | None | DM smoke applies but has not been run on a duel output | Not met | High | Keep |
| Q2 TDM | User choice: 3.19 DM with DF_SKINTEAMS/MODELTEAMS, re-release teamplay, or OpenTDM (installed DLL is x86 only) | 0/8 (DM 4, SP 4); 32-room light 790 s exceeds the 600 s cap | Absent; compact path reduced above 8 rooms | None | Not met; easy to add for 3.19 or re-release | Not met; TDM never mentioned | Medium | Keep if inputs supplied |
| Q2 CTF | 3.19 ctf DLL ("Threewave CTF 1.09b") or re-release `ctf 1` | 7/8 ("q2ctf" → SP) | Far below: legacy path, unmirrored items, team spawns across each half, weapon in flag rooms | None (q2ctf1–5 local) | Not met: no x64 CTF module (installed DLL is x86); smoke is DM-only | Not met; module requirement unstated | Medium | Keep if inputs supplied |
| Q3 bot arena | 1.32b qagame; "bot arena" undefined: GT_SINGLE_PLAYER ladder with .arena, or bot-tuned FFA/tourney | 0/8 (DM 7, SP 1); AAS 8/8 | Far below: no .arena, bot list or intermission; no jump pads | None | Not met: Quake3e dedicated built but never run, takes focus at start; no per-item reachability. GT_SINGLE_PLAYER needs `spmap`, because `map` resets it to FFA (code/server/sv_ccmds.c:179-181), and g_log is not opened in SP (code/game/g_main.c:430) | Partly: README says Q3 SP means tourney/bot arena; omits what is missing | Medium | Keep if inputs supplied |
| Q3 FFA | 1.32b, GT_FFA 0 | 8/8 (5 via the silent Q3 DM default); AAS 8/8 | Far below: legacy ring; machinegun slot, no BFG, 3 RA; no jump pads; near-black walls | None (q3dm6/17 and pro- revisions local) | Not met | Not met | Medium | Keep |
| Q3 tourney | 1.32b, GT_TOURNAMENT 1; "tournament" (entity) vs "tourney" (arena/UI) | 2/8 ("tourney" not recognised) | Far below: no RA/MH/YA trade-off; 2 spawns | None (q3tourney1–6 and pro-q3tourney2/4 local. The community maps ztn3tourney1 and hub3aeroq3 were not found under E:/_SOURCE, E:/Games or E:/Development by a file-name search to depth 6; only a Quake 2 conversion, q2ztn3tourney1, is in the Q2 archive. Availability unverified, licences unreviewed) | Not met | Not met; tourney never mentioned | Medium | Keep |
| Q3 TDM | 1.32b, GT_TEAM 3; DM spawns; weapons 30 s | 0/8 (DM 7, SP 1) | Absent; no gametype/notteam keys | None | Not met; needs bot team assignment | Not met; TDM never mentioned | Medium | Keep |
| Q3 CTF | 1.32b, GT_CTF 4; weapons 5 s | 8/8; contradictions silent; CTest fixture flag unreachable per analyzer | Far below: unmirrored; one team-begin spot per team (telefrag) | None (q3ctf1–5 local) | Not met; needs flag-to-flag reachability for both teams | Not met | Medium | Keep |

No path meets all five conditions today. RC3 and RC5 are unmet on every path. Q2 DM is the only path with engine-initialization evidence, and that evidence does not reproduce from the documented engine root. Tally: keep 6 (Q2 DM, Q2 duel, Q3 FFA, Q3 tourney, Q3 TDM, Q3 CTF); keep if inputs are supplied 5 (Q1 DM, Q1 duel, Q2 TDM, Q2 CTF, Q3 bot arena); decide later 5 (Q1 SP, Q1 co-op, Q1 TDM, Q2 SP, Q2 co-op); likely drop 1 (Q1 Threewave CTF).

### Blockers shared across paths

- **Mode vocabulary and module declaration.** There is no TDM, co-op or bot arena; synonyms are missing; parse precedence is fixed; and Q1/Q2 CTF declare no module. Together these cause every G2 failure. Conflict surfacing would cut the draft total from 70 to 53.
- **Construction depth exists for 2 of 17 paths.** The default planner never selects mechanisms, and no code can emit a Q3 jump pad. This blocks RC2 for 15 paths.
- **The item model is a placeholder in every game, Q2 DM included.** Workstream D therefore raises the parity bar for every path.
- **Materials.**
  - Q1: non-stock names, no `wad` key, and no local Q1 texture WAD at all.
  - Q2: unmanaged SURF_LIGHT ceilings.
  - Q2 and Q3: near-black walls.
  - Q3: missing liquid shaders.
  - Tests assert the broken names, and the gates mask textures.
- **Evidence tooling is Q2-only** (map study, native harness, engine smoke, knowledge cards). RC4 is per game, so the missing Q1 smoke fails every Q1 path and the missing per-item Q3 bot check fails every Q3 path.
- **The Q2 smoke does not reproduce.** The documented root has no build outputs, the pinned binaries survive only in gitignored scratch, the smoke is not in CTest, and it covers deathmatch only.
- **Window focus.** The Q2REPRO client, the Quake3e Windows console (dedicated and client) and the ironwail client all take focus on start. The brief's windowed-screenshot rule needs a user decision for all three games.
- **Q1/Q2 MP runtime faults** (static reading): no `info_intermission` or `info_player_start` in Q1 MP, and no `info_player_start` in Q2 MP.
- **Benchmark policy is not final:**
  - The hard gates omit analysis errors, texture existence, request honouring, determinism and runtime.
  - The 600 s light cap fails a valid 32-room map.
  - Q1 CTF scoring is a policy choice.
  - The runner version does not identify its code.
- **Baseline and provenance.** About 126 changes are uncommitted, there are no tags, `generator_version` does not identify the revision, the evidence sits in gitignored `out/`, and `tools/` and `knowledge/` are untracked. A fresh clone cannot reproduce RC evidence.
- **Brief facts that change design rules** (section 4): Q3 30 s weapons only in GT_TEAM; Q3 powerups first appear after 30–60 s; Q2 NOT_COOP inert in 3.19; Q1 re-release Mega 120 s; 1996 Q1 respawns only at `deathmatch == 1`; Q2 CTF team spawns only for the first spawn; no `trigger_teleport` in base Q2; no shipped Q3 botclip shader.
- **RC5 docs.** README and ROADMAP never mention co-op, TDM, Threewave, bot arena or tourney, and never state that no human playtest has been done.

### External inputs needed (suggested order)

1. **Game contracts.**
   - Q1: the 1996 id1 progs or the 2021 re-release QuakeC.
   - Q2: 3.19 baseq2 or the re-release DLL (this decides whether NOT_COOP matters).
   - Q2 TDM: 3.19 dmflags teams, re-release teamplay, or OpenTDM.
   - Q3: what "bot arena" means (a GT_SINGLE_PLAYER ladder entry with an .arena script, or a bot-tuned FFA/tourney map).
   - Q1 CTF: confirmation that "Threewave" means the 5.0 re-release contract.
2. **Q1 inputs.** A Q1 texture WAD (path, licence, author, hash), or approval to extract a local-only WAD from the user's own id1 BSPs that is never packaged. Then the Q1 smoke engine: build fteqw-sv from local source (reviewable) or pin the ironwail 0.8.2 binary (no source to review).
3. **Windowed-client policy** for the screenshots required in all three games, given that each client takes focus on start. Also the Quake3e dedicated host's console window, or the choice of another Q3 host.
4. **Mod runtimes.** Approval to build a Q2 x64 CTF game module (3.19 ctf or re-release) as a separate GPL process and pin it after source review. For Q1 CTF: a Threewave runtime the user accepts, or approval to build `quakec_ctf` with FTEQCC.
5. **Q2 smoke reproducibility.** Rebuild Q2REPRO at aeeb335f so the pinned hashes reproduce, or re-pin it after a fresh source review.
6. **The RC baseline.** Approval to commit the ~126 uncommitted changes.
7. **Hero briefs and seeds for each kept path,** recorded before generation. This includes the Q2 DM hero seed. For Q2 SP: whether the hero must be a multi-map unit. For Q3: whether one multi-gametype map may serve as the hero for several paths. Also, ztn3tourney1 and hub3aeroq3 are not on disk: will the user supply them, with origin, author, licence and hash, as local-only references? If not, the Q3 tourney study goes ahead without them and says so.
8. **The light-stage time budget** for large maps in the RC benchmark.
9. **Scope.** Whether the SP and co-op paths (Q1 and Q2) and Q1 TDM stay in the rc1 claimed set once workstream E's first acceptance tests are in, and whether co-op and TDM are claimed separately or as documented uses of the SP and DM paths.

---

Evidence levels used below, from weakest to strongest. The brief names five levels: static analysis, compiler output, native movement trial, engine initialization and human play. This plan also uses three intermediate levels that the brief's ladder does not name. They are marked *(added)*, and their names need approval (D34).
- **static analysis**: code reading, analysis of generated output, and checks of documents and schemas.
- **compiler output**: qbsp/VIS/light, q3map2 or BSPC.
- **AAS routing** *(added)*: pinned, unmodified id botlib code run over a compiled AAS file in a separate process (`tools/q3host`). It shows that botlib's route finder can connect two entities. No bot runs and no game frame is simulated.
- **native movement trial**: pinned id movement code run in a separate process with scripted movement commands. There is no engine, and no input is injected into any program.
- **engine initialization**: a dedicated server loads the map. No player is connected.
- **engine runtime with bots** *(added)*: Q3 only. Server-side bots, no human input.
- **engine rendering** *(added)*: the engine's own `screenshot` command in a windowed client.
- **human play**.

GGUF results are labelled **model inference**. No human has played any generated map. This plan claims human play nowhere.

**Fact corrections made while revising this plan (2026-10-15, static reading of local id source):**
- A Q2 dedicated server forces `deathmatch 1` unless `coop` is set (Quake-2-master/server/sv_init.c:318-324). In deathmatch, `monster_start` frees every monster (game/g_monster.c:534-538). The q2repro-current checkout (commit 1523f113, not the pinned aeeb335f) does the same (src/server/init.c:437-442). A "deathmatch 0" SP profile on a dedicated server would therefore silently test a DM map.
- In Q3 1.32b, loading a map with `map` resets `g_gametype 2` to FFA (code/server/sv_ccmds.c:179-181). GT_SINGLE_PLAYER needs `spmap` (sv_ccmds.c:162-169). The game log file is never opened in GT_SINGLE_PLAYER (code/game/g_main.c:430), but on a dedicated server `G_LogPrintf` echoes every log line to the console (g_main.c:1095-1097). Remote challenges and status queries are ignored in SP (sv_client.c:53, sv_main.c:318). Quake3e behaves the same way under `map` (Quake3e-master/code/server/sv_ccmds.c:202-204).
- Q2 3.19 health and ammo respawn after 30 s, in deathmatch only (game/g_items.c:581-582 Pickup_Health, 508-509 Pickup_Ammo). Audit section 4 already listed them. Weapons stay in Q2 coop (game/p_weapon.c:125-130, 152-153).
- Q1 1996 progs and the re-release QuakeC both end in `objerror` when FindIntermission finds no `info_intermission`, `info_player_start` or `testplayerstart` (1996 progs FindIntermission statements 5672-5715, called from execute_changelevel at statement 5878; re-release quakec/client.qc:135-168).
- Q1 ambush is spawnflag 1, read as `spawnflags & 3` in FindTarget (re-release quakec/ai.qc:299-305; 1996 FindTarget statements 984-988). A Q1 teleport-in wave is a targetnamed `trigger_teleport` that acts only after it is fired, with PLAYER_ONLY (1) clear (re-release triggers.qc:397-418; 1996 teleport_touch statements 10251-10270). Q1 has no trigger-spawn monster flag.
- The Q2 G13 band "≤ 25.5 %" is q2dm3's share of floor luxels below 16, the highest among q2dm1–8 and base1. It comes from the Phase-1 lightmap probe (local `out/benchmark-draft/probes/lighting-calibration/calib.py`, rerun on 2026-10-15 with identical rows). The same rerun shows that stock saturation is not near zero: dm2 has 18.0 % and q3tourney2 23.9 % of floor luxels at 250 or above. The earlier "saturated ≤ 5 %" band was unsourced and is replaced.
- ztn3tourney1 and hub3aeroq3 were not found under E:/_SOURCE, E:/Games or E:/Development (file-name search, depth 6). The only match is a Quake 2 conversion, q2ztn3tourney1, in E:/Development/q2mapsarchive, which is not a Q3 reference.

## 8. Plan

### 8.1 Ordering principle

Workstreams are ranked by what a player meets first, and most often, after loading a generated map:

1. **The map is the game and mode they asked for.** Its spawns work and a match ends without an error.
2. **There is something to fight over.** Weapons, armor, health, ammo and powerups are placed by role, timed from the game source, and spawns are safe.
3. **The space can be seen and read.** Textures exist, light comes from visible sources, and compiled brightness sits in the range stock maps use.
4. **Routes and height give real choices.**
5. **For single-player, the level is a designed progression that can be beaten.**

Before any of this, the person typing the prompt needs an honest account of what was honoured. That is why the brief work (A) sits second, straight after modes.

Within this order, severity counts first. A crash, telefrag or wrong mode outranks wrong or missing content, which outranks appearance, which outranks variety. Breadth breaks ties: how many of the 17 claimed game × mode paths a gap affects, and how often. Under this rule the crash and telefrag fixes that are only entity additions land first, in Phase 2 (rank 1, part R1-s). They do not wait for the larger mode-vocabulary work.

Measurement (X1), game profiles and the asset registry (X2), runtime evidence (H) and the map study (B) do not change maps on their own. They are ranked by how directly they catch failures players would hit. They are **scheduled** as early as the acceptance tests that rely on them require.

### 8.2 How dependencies modify the order

Player impact sets the rank. Hard dependencies set the start. The following items start ahead of their rank:

| Item | Rank | Must start in | Why |
|---|---|---|---|
| X1 Measurement: benchmark v1, gate fixes, baselines, RC ledger | 0a | Phase 2, first | Every acceptance test below is measured against its fixed sets and gates. Without it no change can be shown to help, and the default compact Q2 path has no compiler gate. |
| X2 Game profiles, fact tables, asset registry | 0b | Phase 2 | D, E, F and G all need cited timers, classnames, eye heights and admitted texture metadata. Moving those tables out of switch statements first, with byte-identical output, stops each workstream from re-deriving facts. |
| X1 repair fail-closed fix | 0a | Phase 2 | Repair re-runs the generic allocator over compact Q2 layouts and drops the station binding, spawn refinement and motif lights (repair.cpp:450-458). D and G cannot rely on their placements until this is fixed. |
| R1-s entity safety fixes (part of rank 1) | 1 | Phase 2, after baseline-0 | These are the highest-severity defects by 8.1's own rule, and each is only an entity addition or a placement check: Q1 `info_intermission` in every map; `info_player_start` in every Q1/Q2 MP map; `info_player_coop` in every Q1/Q2 SP map; enough Q3 CTF team-begin spots; hull-aware support tests for large monsters and SP pickups (audit gaps 6–7). Each is its own switched cause (D7) and needs only build, ctest and the compiler gate. They are the only generator-output changes allowed in Phase 2. |
| D-a item catalogue and IR (part of rank 3) | 3 | Phase 3 | It depends only on X2 and D8. The missing weapons (Q1 and Q2 SSG, Q2 and Q3 BFG), matching ammo, Mega Health and armor tiers, and dropping the Q3 machinegun slot are what players meet most often. Only the qualifier verdicts and the role solver (D-b) need rank 2, so those stay in Phase 4. |
| H1 Q2 smoke made reproducible | 8 | Phase 2 | It is the only engine evidence in the project, and it cannot be rebuilt from its documented root. Rank 1 needs it for co-op and CTF acceptance in Phase 3. |
| H2 Q3 per-entity reachability (`q3host`) and Q3 smoke | 8 | Phase 3 | RC4 needs it, and C2 (jump pads) and the Q3 heroes are judged by it. It changes no generator output, so it can be built in parallel. |
| B1 BSP29/IBSP46 readers, stock calibration | 9 | Phase 2–3 | The D, F, G and C thresholds stay provisional until stock bands exist. The Q2 hero needs q2dm1–q2dm8 measurements. |
| F1 visible material defects | 4 | Phase 3 | These are small, independent fixes with a large visible effect: Q2 lamp-tile ceilings, near-black walls, hidden faces, Q1 WAD support. G cannot be calibrated while the Q2 ceilings flood every map. The Q2 emitter removal lands behind its switch in Phase 3, but its default flips only with the Q2 light rescaling in Phase 4 (D33). |
| G1 compiled-lightmap probe, in report-only mode | 5 | Phase 3 | The F1 Q2 change darkens every Q2 map (mean lightmap about 152 to about 20, measured on one map). The probe has to exist to record that interim state. |
| C1 Q2 construction slice | 6 | Phase 4 | The Q2 hero's "three floor levels", "cut into a canyon wall", "central furnace landmark" and "cold outdoor sky" need it, and G2's sun and sky need outdoor volumes. |
| X3 docs truth patch | 10 | Phase 2 | It costs little. README and ROADMAP never mention co-op, TDM, Threewave, bot arena or tourney, and do not say that no human playtest has been done. |
| E1 SP go/no-go slice, with C6a (progression-graph skeleton) | 7 | Phase 5, in parallel, on the SP sub-lane | E is XL. An early checkpoint keeps the RC from waiting on paths that may be dropped. E1 needs the C6a skeleton (start, keys, exit, no start-to-exit loop edge), so C6a moves into Phase 5 with it. C6b (loop-back, views ahead, arenas) stays in Phase 7. The Phase-5 checkpoint covers Q2 SP and Q2 co-op only. Q1 SP and co-op are checked at the Phase-6 exit, once the Q1 WAD and Q1 smoke exist. |

### 8.3 How measurement works

**Fixed sets.** These are frozen once, in Phase 2. Prompt text is copied into tracked manifests and each manifest's sha256 is recorded. Cases may be added under new ids. Existing cases are never edited.

| Set | Contents | Purpose |
|---|---|---|
| **BENCH-v1** | `tools/benchmark/rc1-v1.json`. The 138 draft-v0 cases verbatim (same prompts, seeds and expected_requests; draft manifest sha256 `056889a0…`), so draft-v0 remains baseline-0. New ids are added for parse regressions, item-specific cases (at least 8 per game), mechanism cases (at least 8 per mechanism per game where the game supports it), Q1 CTF prompts that name Threewave, size, mood, forbidden-item and landmark requests, and SP cases with keys, secrets and objectives. Each case carries `claimed`, `module`, `path` (one of the 17), `expected_conflicts` {class, span} and `expected_resolution`. **Sealed case:** q2-showcase-hero (seed 4242) is listed in a separate tracked seal list (`tools/benchmark/sealed.json`), so the case itself is not edited. After baseline-0 has been recorded, routine runs skip it, and only its prompt is checked, through the `qmg brief` dry run, which generates nothing. It is generated again only in the hero run (D26). | RC1, gates G1–G15 |
| **RC1 set per path** | Every BENCH-v1 case whose `path` is that path: the 8 core cases (3 vague, 3 specific, 2 contradictory) **plus** every case added for that path. Sealed cases are excluded until the hero run. | RC1: 100% of the set passes every hard gate |
| **MECH-SAMPLE** | For each mechanism kind and game, the BENCH-v1 mechanism cases, plus the first 2 vague BENCH-v1 cases (in id order) per path in which the default planner chose that mechanism without being asked. The selection rule is fixed now; the cases are chosen by that rule, not by inspecting the maps. | Rubric P4: evidence for default-chosen mechanisms |
| **CONTRA-v1** | The 34 contradictory cases (2 per target), plus conflict regressions. | G10 |
| **PARSE-MATRIX-v1** | A unit-test table. Mode phrases × 3 games. Negations ("no loops", "not vertical", "no water"). False positives ("database", "two bases", "open space"). Numbers ("eight players", "2–8" with U+2013, "2-8", "2 to 8", "for 16 people"). Map-name references ("a q2dm1 style map", "q3dm17 remake for Quake 2"). | Rank 1 and rank 2 |
| **MP-PORTFOLIO** | Weft nine: rooms 4/6/8 × seeds 17/42/99, plus rooms6-seed42 high/low. | Q2 DM regression |
| **SPAN-PORTFOLIO** | The `out/span-portfolio/protocol.json` cases, pinned by prompt sha256. The Lattice fifteen and Splice nine portfolios are copied into tracked manifests. | Q2 DM regression, variety gate |
| **STOCK-Q1 / Q2 / Q3** | Q1: id1 dm1–dm6 and e1m1–e4m8 (51 BSPs for parsing), plus 70 Q1RM BSPs. Q2: q2dm1–8, base1–3 and the other local units, q2ctf1–5. Q3: q3dm0–19, q3tourney1–6, q3ctf1–5, pro-q3dm6/13, pro-q3tourney2/4, each with its shipped .aas. Calibration subsets are named in each test. | Calibration, counterexamples |
| **CTEST-FIXTURES** | The existing compiler fixtures plus new compact Q2 dm6/duel5/dm32 and mechanism fixtures. | Per-change gate |
| **CONTROLS** | Project-authored positive and negative fixtures, one or more per gate, such as a "10 empty lightmaps" log or a buried RL. No reference geometry. | Gate validity |
| **HEROES** | `tools/benchmark/heroes.json`. Per path: prompt, seed, expected requests, any requests declared out of scope, reference set, evidence plan (native routes, bot-run K and frame budget, screenshot views). Committed before that hero is generated. A hero's primary seed appears in no other fixed set. | RC3 |

**Gates.** G1–G7 keep their draft-v0 definitions, with the fixes noted below. G8–G15 begin as **reported** columns. Each becomes **hard** when the workstream that owns it passes its exit gate. That way baseline-0 stays comparable and later "passes" are not loosened.

| Gate | Checks | Owner | Hard from |
|---|---|---|---|
| G1 | Generated. A contradictory case must still generate unless its manifest entry predeclares a refusal (decision D11). | X1 | now |
| G2 | Intent: game, primary mode, module, player range. | Rank 1 | now (mapping extended) |
| G3 | Leak-free compile. | X1 | now |
| G4 | Full VIS. Anchored marker; a `vis -fast` negative control fails. | X1 | Phase 2 |
| G5 | Final light. Anchored "0 empty lightmaps"; nonempty lump; within the light budget. | X1 | Phase 2 |
| G6 | Q3 AAS valid (BSPC global consistency). | X1 | now |
| G7 | Strict mode-entity contract, read from the compiled entity lump. | Rank 1 | Phase 3 exit |
| G8 | Determinism: byte-identical .map and report across two processes. | X1 | Phase 2 exit |
| G9 | Request accounting: zero silent drops. | Rank 2 | Phase 3 exit |
| G10 | Conflict surfaced, with prompt spans and the documented resolution. | Rank 2 | Phase 3 exit |
| G11 | Error-severity analysis findings, under the waiver rules below. | H5 | MP navigation and reachability errors (unreachable placements, flags and spawns): Phase 3 exit. `unlit_critical_placement`: at each game's lighting exit (G2), Phase 4 for Q2, Phase 5 for Q3, Phase 6 for Q1, because lighting is report-only until then. SP errors: Phase 7. |
| G12 | Materials exist, compiled against real assets (no `-notex`, no `$whiteimage` stubs). | F | Phase 3 exit (Q1 only with a WAD) |
| G13 | Compiled lighting readability, within the stock band. | G | per game at that game's depth exit |
| G14 | Engine smoke under the mode profile. | H | per game as its harness lands |
| G15 | Q3 per-entity bot reachability. | H2 | Phase 5 |

**Waivers (D11(8)).** Wherever a test below allows a waiver or a declared exception, these rules apply:
- Each waiver has a reason code from a closed list in `tools/benchmark/README.md`, the finding or instance id, and the user's approval recorded in the ledger. The agent cannot approve its own waivers.
- Waivers are capped at 5% of the measured instances per gate per path, and at 0 on heroes unless the hero's HEROES entry declared the exception before generation. Above the cap the gate fails.
- The waiver count and share are reported beside every pass rate that uses them.

**Light budget.** This is provisional. Final light must finish within 1200 s at the fixed concurrency of 8 jobs × 2 threads, which replaces the 600 s cap that failed q2-tdm-c2. The 1200 s figure is 1.5 × a **solo** time (q2-tdm-c2 took 790 s alone), but the budget is enforced under concurrency, and fixtures, sun and bounce will add time. In Phase 2 the 10 slowest BENCH-v1 cases are rerun at 8 × 2, and the budget is frozen at 1.5 × the slowest time measured under that load. In Phase 4 it is re-measured once with G2 fixtures and sun. Each change of the budget is recorded in D11(3), and the gate is never loosened afterwards.

**Change protocol.** Every change follows these steps:
1. `cmake --build --preset windows-clang` and `ctest --preset windows-clang` pass.
2. A serialization or shell-construction change also passes the real Q1/Q2/Q3 compiler fixtures.
3. The change has **one cause**. It lands behind a stage-profile switch that is off by default, with default output byte-identical to the previous baseline.
4. The affected fixed sets are run with the switch on and off. `tools/benchmark/compare.py --scope <declared>` diffs report fields, sorted brush planes, the entity multiset and the texture histogram. It must show 0 differences outside the declared scope; any out-of-scope difference fails the change. A Q2 DM change also reruns MP-PORTFOLIO and SPAN-PORTFOLIO.
5. An experiment record (`knowledge/<game>/experiments/<id>.json`) gives the hypothesis, expected mechanism, before and after values, scope diff and verdict.
6. The default flips in a separate commit, and the baseline is re-recorded as baseline-N in a tracked baselines index (summary hashes only; outputs stay local under `out/`).
7. Failed variants, counterexamples and earlier cards and experiments are kept. Nothing is deleted.

Changes that alter generator output are made **one at a time per fixed set**. There are two output sub-lanes with disjoint scopes:
- **MP sub-lane**: every DM, duel, tourney, TDM, CTF and bot-arena case.
- **SP sub-lane**: the Q1/Q2 SP and co-op cases, the SP CTEST fixtures and the unit prompts.

Each sub-lane has at most one output change in flight. A change declares its sub-lane, and `compare.py --scope` must show 0 byte differences on every case of the other sub-lane, or the change is rejected. A change that touches both sub-lanes, such as a prompt-normalization change, waits until neither sub-lane has a change in flight. This is what lets E1 run alongside the Phase-5 MP work without breaking the one-cause rule. Harness, reader and docs work does not touch generator output and runs in parallel.

**Determinism.**
- Every stage is a pure function of: the canonical normalized brief, plan, candidate seed, stage revision, and profile and registry sha256.
- Each stage draws from its own stream (splitmix64 of candidate_seed XOR a stage tag), so inserting a stage never shifts earlier draws.
- Only the project's own `uniform_integer` is used, never std distributions.
- The GGUF model revision and `n_threads` are part of the key.
- Repair re-runs the stage chain, not the legacy `EntityAllocator`.

**Other measurement rules.**
- The models only propose constrained semantics. Project code builds and validates every brush.
- Assets are added only with origin, licence, author and sha256 recorded.
- Reference outputs stay under `out/`, and reference geometry is never copied. Tracked files may hold only the stock-derived aggregates that D29 permits: scalar bands, medians and counts, plus texture and shader names, hashes and metadata. Each aggregate records its source map ids and the hash of the script that derived it. Geometry, entity lumps, lightmaps, image bytes and per-map raw outputs are never tracked.
- Calibration is descriptive, not training (D30). Bands and thresholds are percentiles or medians of stock measurements, chosen by a recorded rule and reviewed by a person. No generator or ranker parameter is fitted by optimisation against reference maps until rights are reviewed. Ranker weights stay hand-set, as today (src/ggml_ranker.cpp:126).
- GPL engines and tools run only as separate processes.
- Engines run windowed. Rendering evidence comes only from the engine's `screenshot` command, and input is never injected.

## 9. Workstreams in order of player impact

Ranks 0a and 0b are enabling work. Ranks 1–10 are in player-impact order. Each workstream lists its A–H area. The architectural changes cite decisions in section 11.

### 9.1 Rank 0a — X1 Measuring stick: benchmark v1, gate fixes, baselines, RC ledger (H; RC1, RC2)

**Why it matters to players.** It matters indirectly, but no later player-facing claim can be measured or protected without it.
- 17 of the 70 draft full passes are contradictory prompts resolved silently. Gating on conflict surfacing gives 53/138.
- The light check matches "0 empty lightmaps" as a substring, so "10 empty lightmaps" passes. "calculating full vis" is also printed by `vis -fast` (cmake/RunClassicVisLight.cmake:164-171).
- The default compact Q2 DM/duel path, which produces the Q2 MP maps players get today, has no CTest compiler fixture (src/generator.cpp:223-226 against cmake/RunQ2CompilerTest.cmake:141-143).
- Two CTest groups read the gitignored `out/`, so a clean clone fails them.
- `generator_version` 0.4.0 and `RUNNER_VERSION` 0.1.0 do not identify the code.
- Maps that pass every gate still ship 11 unlit_critical_placement, 116 unreachable_monster and 240 unreachable_timing_target findings.

**Scope.**
- Freeze the fixed sets in 8.3.
- Runner identity: `RUNNER_VERSION` is the sha256 of run.py. Each result.json binds the runner, manifest, qmg.exe, source revision and tool-pin hashes. The summary refuses mixed runner hashes.
- Add G8–G15 as reported columns and harden the CTest compiler gates.
- Add `compare.py` scoped diffs, the experiment-record template and the change protocol (docs/CHANGE_PROTOCOL.md).
- Fix the repair fail-closed defect.
- Add the RC ledger and parity rubric.
- Add an opt-in, hash-gated `rc-benchmark` CTest label, plus a PR-time subset.
- Baseline-0 is the Phase-1 build (qmg.exe `815bb58d…`) run on BENCH-v1.

**Parity rubric (proposed RC2 bar, decision D12).** A path is "at Q2 DM depth" when it has evidence for each of these:
- **P1.** A dedicated mode-aware construction path. Geometry differs across games and modes at a fixed seed.
- **P2.** At least 2 layout families, at least 3 vertical motifs, a barrier/cover motif and at least 1 inter-room vertical link, each with typed access contracts and access-removal unit tests (the current `test_mp_motifs` standard).
- **P3.** Item/spawn station binding and spawn LOS refinement at the game's eye height.
- **P4.** Mechanisms chosen by the default planner, without a prompt request. Metric (provisional): at least 50% of the path's vague BENCH-v1 and portfolio maps with 2 or more floor levels contain at least 1 default-chosen mechanism, and 100% of default-chosen mechanisms pass the recipe audits. Every mechanism kind the path uses has the evidence the recipes had, on MECH-SAMPLE: a native traversal (where a harness exists) and engine initialization.
- **P5.** The item model and timing simulation (D).
- **P6.** Materials and lighting (F, G).
- **P7.** Navigation, visibility, lighting and quality analysis with game constants, with error findings gated.
- **P8.** A CTest compiler gate on the default path at two scales.
- **P9.** Native movement trials on the hero's main routes: 100% of the predeclared routes complete, and failures are kept.
- **P10.** Engine initialization on bench cases and the hero.
- **P11.** The variety gate.

SP and co-op have their own rubric: a progression graph with key gating, loop-back and views ahead (C6a, C6b); at least 5 typed encounter kinds; per-skill plans; a resource model with a completability check; hinted secrets; coop starts and the co-op output model chosen in D28.

**RC1 per path** means that 100% of the path's RC1 set (8.3) passes every hard gate: the 8 core cases plus every case added for that path. "8/8" below always refers to the core cases, never to the whole set.

**Deliverables.**
- `tools/benchmark/rc1-v1.json` (frozen), `heroes.json` (schema only at first), `sealed.json`, and a gate-policy section in `tools/benchmark/README.md` that covers the waiver reason codes and cap.
- run.py v1 with G8–G15, stage timers and Q3 liquid/sky detection by shader surfaceparm instead of name substring. `compare.py` with a self-test.
- cmake: an anchored light regex; a full-VIS check that rejects `-fast` output; `qmg_q2_compact_{dm6,duel5,dm32}` with full VIS and final light plus a leaky variant; Q3 final light on one fixture larger than 4 rooms; no CTest group importing from `out/`. When a tool is missing, the test reports "not configured" instead of passing.
- `qmg_determinism_matrix` CTest: at least 2 cases per game × mode, two processes, byte compare.
- Repair fails closed instead of re-allocating compact Q2 layouts, with a unit test.
- `tools/benchmark/rc-ledger.json` with a schema. Per path: status, RC1–RC5 evidence pointers, the human-playtest field. Plus `parity.py` and CTests `qmg_rc_ledger` and `qmg_docs_claims`.
- `QMG_VERSION` carries the source revision (report v5 field, D2).

**Acceptance tests.**

| Test | Metric | Threshold | Fixed set | Evidence level | Tool |
|---|---|---|---|---|---|
| Baseline-0 reproduces draft | Per-gate counts under the draft policy | Identical: G1 138, G2 70, G3 138, G4 138, G5 137, G6 40/40, G7 85/29/24, all-gates 70 | BENCH-v1 draft-v0 subset, qmg `815bb58d…` | compiler output | run.py v1, cross-checked by the independent `bench-verify/verify.py` |
| Gate controls | Classification of each gate's controls | 100%. At least 1 positive accepted and 1 negative rejected per gate, including: a "10 empty lightmaps" log; a `vis -fast` BSP; a leaky box; a missing texture compiled without `-notex`; a corrupt AAS; a report with an unreported unsupported request | CONTROLS | compiler output (G3–G6, G12); static analysis (others) | `ctest -L benchmark-controls` |
| G8 determinism | Byte equality of .map and .qmg.json across 2 processes | 100% of BENCH-v1. Opt-in GGUF: 100% two-run identity at fixed `n_threads` | BENCH-v1, GGUF case set | static analysis (GGUF part: model inference) | `run.py --repeat 2`; `qmg_determinism_matrix` |
| Compact Q2 compiler gate | Leak-free, full VIS, final light | 3/3 pass; the leaky variant fails; all pre-existing CTest groups still pass (64/64 today) | CTEST-FIXTURES (dm6, duel5, dm32, seed 42) | compiler output | `ctest -R qmg_q2_compact` |
| Light budget | Final-light wall time at 8 × 2 | First, the 10 slowest cases are rerun at 8 × 2 and the budget is frozen (8.3). Then 100% of cases finish within the frozen budget (provisionally 1200 s), the 32-room cases included | BENCH-v1 | compiler output | run.py stage timers |
| Clean clone | CTest groups that depend on `out/` | 0. A fresh worktree without `out/` passes every non-optional group | Full CTest suite | static analysis | ctest in a fresh git worktree (after D1) |
| Change records | Output-changing commits with build, ctest, affected-subset before/after, scope diff, and a compiler gate where required | 100% | Every change from Phase 2 on | static analysis | ledger checker plus `compare.py --scope` |
| Ledger integrity | Resolving pointers and matching hashes; claimed paths with any rubric item lacking evidence; waivers without a reason code or user approval, or above the 5% cap | 100%; 0; 0 | All 17 paths | static analysis | `ctest -R qmg_rc_ledger`; `parity.py --strict` |
| Hero seal | Generations of a sealed case on any build after baseline-0, outside the hero run | 0 (run.py refuses, and the ledger records any attempt) | `sealed.json`, run.py logs | static analysis | run.py seal check plus `qmg_rc_ledger` |
| Rubric self-check | Q2 DM rubric items failing at baseline-0 | Recorded as failing, as expected: P8 (no compact gate), P10 (smoke predates the build), P11 (Splice 8-room variety fell from 2 to 1). They must close by the Phase-4 exit. | MP-PORTFOLIO, BENCH-v1 Q2 DM/duel | static analysis | `parity.py --path q2-dm --path q2-duel` |

**Depends on.** Nothing. The clean-clone test needs D1.

**Architectural changes (need approval).** Benchmark v1 as a versioned contract under an opt-in CTest label, plus the gate policy (D11). Report v5 source-revision field (D2). Definition of the RC2 parity bar (D12). Tracked ledger and rubric files.

**Risks.**
- Until D1 is decided, the fixed sets exist only in the working tree.
- A full BENCH-v1 run takes hours of final light (Q2 takes 165–330 s per 5–10-room case under load). It stays opt-in and overnight, with a PR-time subset.
- Hardening the regexes may turn earlier passes into failures. That is recorded, and the gate is not loosened.
- Some expected_requests name vocabulary added only later. They stay unmet, with findings, until then. That is intended, and the cases are not edited.

**Size.** L.

### 9.2 Rank 0b — X2 Game profiles, id-source fact tables, asset registry (supports D, E, F, G)

**Why it matters to players.** Indirect. Wrong or missing facts are scattered through switch statements (map_builder.cpp:242-368, 876-975; entities.cpp:154-337). Examples:
- The Q1 SSG is missing.
- The Q3 machinegun takes a weapon slot, though players spawn with it.
- WALL1_1 and FLOOR0_1 are not stock names.
- e1u1/ceil1_4 inherits SURF_LIGHT 10000.
- e1u1/water1, slime1 and lava1 are not in baseq2.
- The `qmg/*` Q3 shaders do not exist.

One cited table per game and module lets D, E, F and G be correct for all three games at once.

**Scope.**

*GameProfile per (game, module).* It holds movement constants and eye offset (each read from id source before encoding: Q1 client.qc view_ofs, Q3 bg_public.h DEFAULT_VIEWHEIGHT; Q2 +22 today), the item catalogue with tiers, respawn timers keyed by mode and settings, spawnflag semantics, classnames and mode-entity contracts. Every value cites the facts digest by file:line, or by function and statement index for the 1996 progs. The corrected facts are encoded as found:
- **Q3 1.32b:** weapons 5 s, 30 s only in GT_TEAM (CTF stays at 5 s); armor 25 s; health and MH 35 s; ammo 40 s; holdables 60 s; powerups 120 s after pickup, first spawn 30–60 s after map start. Per-entity `wait` and `random` override all timers.
- **Q2 3.19:** weapons 30 s; jacket/combat/body/shard 20 s; power screen and shield 60 s; health 30 s (g_items.c:581-582 Pickup_Health); ammo 30 s (g_items.c:508-509 Pickup_Ammo); silencer, rebreather, enviro suit, adrenaline, ancient head and bandolier 60 s; ammo pack 180 s (g_items.c:197-332); MH 5 s + decay + 20 s; Quad 60 s; Invulnerability 300 s. Every timer applies only in deathmatch; in coop, weapons stay and nothing respawns (p_weapon.c:125-130, 152-153). NOT_COOP 4096 is inert (g_spawn.c:587). Base trigger_teleport exists only in the CTF DLL.
- **Q1 1996 progs:** weapons, armor, ammo and MH respawn only when deathmatch == 1. In coop, weapons stay and no item respawns. Skill flags apply to SP and coop alike, because both run with deathmatch == 0 (WinQuake pr_edict.c:931-948), so no spawnflag tells co-op apart from SP. Total kills count each fish twice (1996 swimmonster statements 8606 and 8682). **Q1 re-release:** MH respawns a fixed 120 s after pickup; fish count once. **QW:** ignores skill flags.

*Asset registry.* A pinned tool runs as a separate process. It reads local game data and writes JSON: name, sha256, dimensions, Q2 WAL flags/contents/value, Q3 shader file:line, surfaceparms, q3map_surfacelight and skyparms. **It stores no asset bytes.** Tracking this retail metadata needs explicit approval as a D29 exception (D9(1)). If that is refused, the registry is written under `out/` and only its sha256 is tracked. A Q1 WAD is supplied by the user, and its path and hash go in the report. Tables are compiled in from the JSON as a generated header, and their sha256 goes in report v5.

**Deliverables.**
- `data/profiles/{q1,q2,q3}/*.json` (schema `qmg.game_profile` v1) with a CTest schema and citation validator.
- `tools/asset_registry` writing `data/assets/{q2,q3}/registry.json` if D29 allows it, otherwise `out/assets/` with only the hash tracked. The Q1 registry is built per run from the user's WAD.
- The admission-check library used by F.
- `classname_for`, `variant_class` and `material_for` refactored to read the profiles, with **no behaviour change in the same commit**.

**Acceptance tests.**

| Test | Metric | Threshold | Fixed set | Evidence level | Tool |
|---|---|---|---|---|---|
| Citation coverage | Profile values without an id-source citation; values disagreeing with the facts digest | 0; 0. The digest's discrepancies are encoded as corrected, with notes | All profile JSON plus a facts fixture | static analysis | `ctest -R profile_schema` |
| Refactor equivalence | Byte differences after moving the tables into profiles | 0 | BENCH-v1, SPAN-PORTFOLIO, MP-PORTFOLIO | static analysis | `compare.py` baseline-N against X2 |
| Registry negative controls | Admission verdicts | All rejected: WALL1_1/FLOOR0_1 without a WAD; e2u1/mud1_1 as a floor (the Talus lesson); ceil1_4 as a plain ceiling; comp3text as a wall; e1u1/water1 (absent); qmg/lava with no shipped shader. Verified stock controls accepted | Recorded control list | static analysis | `ctest -R asset_admission` |
| No asset bytes | Image, WAL, BSP or WAD magic in tracked data | 0. Every entry has source path, origin and licence-status fields | `data/` | static analysis | CTest byte scan |

**Depends on.** X1.

**Architectural changes (need approval).** A tracked `data/` tree with schemas `qmg.game_profile` v1 and `qmg.asset_registry` v1, compiled in (D10). Retail texture and shader names, hashes and metadata, with no bytes, in tracked files (D9, D29).

**Risks.**
- The 1996 Q1 facts are known only by disassembly, and the KEX source is not local. Those values carry their evidence basis.
- This is a wide refactor. Byte equivalence comes first.
- Q2 re-release DLL timers are not examined. Only 3.19 values are encoded.

**Size.** L.

### 9.3 Rank 1 — Right game, right mode, safe spawns: mode vocabulary, game module, mode-entity contracts (A mode layer; D mode entities)

**Why it matters to players.** This is the first thing a player hits.
- 56/138 benchmark cases ask for modes the generator cannot represent: co-op, TDM, bot arena, Threewave.
- 12 representable cases still resolve to the wrong mode, giving 68 G2 failures in all:
  - q1-dm-v3, q1-duel-v3, q2-duel-v3 ("LAN party", "two players", "one-on-one" become SP);
  - q2-ctf-v3 ("q2ctf" becomes SP);
  - q2-sp-c1;
  - q3-tourney-v1/v2/s1/s2/c1/c2 (tourney is not a mode word);
  - q2-showcase-hero ("1v1" outranks "deathmatch", src/prompt.cpp:204-221).
- Reading the id source shows several runtime faults:
  - When a generated Q1 DM/CTF match reaches its fraglimit or timelimit, it should end in a fatal QuakeC "Program error". The map has no info_intermission or info_player_start, so FindIntermission calls objerror. Both progs were read: the 1996 progs (FindIntermission statements 5672-5715, reached from execute_changelevel at 5878) and the re-release QuakeC (client.qc:135-168). A server with no fraglimit or timelimit never reaches this code.
  - A Q1/Q2 MP map loaded with `map` and deathmatch unset aborts.
  - Co-op players 2 and up telefrag on the single start (Q2 KillBox, Q1 spawn_tdeath).
  - Q3 CTF has one team_CTF_*player per team, so teams of 3 telefrag on team begin.
  - Q1 CTF has no team starts.
  - Q3 "campaign" prompts come out as an FFA arena labelled single_player.

None of this has been engine-tested. The evidence level is static analysis and code reading.

**Scope.** Rank 1 is split into separately switched causes. Each has its own switch (D7), measurement and baseline-N, landing in this order:
- **R1-s (Phase 2) entity safety.** Each item lands alone: (1) Q1 `info_intermission` in every map; (2) `info_player_start` in every Q1/Q2 MP map; (3) `info_player_coop` × 3 in every Q1/Q2 SP map, which is harmless in SP and stops co-op telefrags on SP maps; (4) Q3 CTF team-begin spots per team; (5) hull-aware support tests for large monsters (audit gap 6); (6) the same for SP pickups (audit gap 7).
- **R1-a** GameMode and GameModule enumerators, plus the CLI options.
- **R1-b** Per-game mode synonyms, including tourney.
- **R1-c** Prompt-order precedence and compatibility clauses.
- **R1-d** Player-range parsing, and removal of the duel and CTF coercions.
- **R1-e** Map-name references treated as style hints.
- **R1-f** The contradiction detector and the unservable table.
- **R1-g** The mode-entity contracts that depend on the new modes: coop counts from player_max, team starts, the Q3 gametype keys, `.arena` output.
- **R1-h** The D5 determinism-key change, last, because it changes every layout_sha256.

*Brief side.*
- New GameMode values: `cooperative` (Q1/Q2), `team_deathmatch` (all three games) and `bot_arena` (Q3, defined by D15).
- Q3 "tourney" and "tournament" resolve to duel. Entity gametype keys use the token `tournament`, and arena/UI files use `tourney` (g_spawn.c:412 against ui_startserver.c:136).
- `GameModule` set by decision: Q1 id1 1996 or re-release, Threewave 5.0; Q2 baseq2 3.19 or ctf 3.19; Q3 baseq3 1.32b with a gametype set. It is declared in report v5 and in worldspawn `_qmg_module`, with recommended server settings (deathmatch, coop, teamplay, g_gametype, dmflags).
- Per-game mode synonyms. Prompt-order precedence. Compatibility clauses ("also works for", "also playable as") give a primary mode plus `compatible_modes`.
- Player range: min and max from digits, number words, hyphen, en dash or "to". Duel no longer forces 2 players; CTF no longer forces at least 6. Silent coercion becomes typed findings.
- Map-name references are style hints and never switch game or mode.
- A contradiction detector applies documented resolutions. A combination that cannot be served fails closed with a typed error only if it is in a published "unservable" table.
- The determinism key moves to the canonical normalized-brief hash. The raw prompt hash stays in the report for one version.
- CLI: `--mode coop|tdm|botarena` and `--module`.

*Entity side.*
- info_player_start in every Q1/Q2 map, MP included. Its presence must not change DM spawn selection. That is checked statically against the selection code, because dedicated initialization cannot observe spawn selection (section 13). Q2 SelectSpawnPoint uses info_player_start only when the deathmatch search finds no spot (p_client.c:884-905). The Q1 1996 SelectSpawnPoint searches info_player_deathmatch before any info_player_start (statements 6031-6074).
- An intermission point in every map: Q1 info_intermission (at least 1, facing a landmark); Q2/Q3 info_player_intermission.
- At least player_max − 1 info_player_coop near the start, with no hull overlap. Q2 coop starts get a targetname matching the arrival spawnpoint (empty for direct loads).
- Threewave: info_player_team1/2, at least ceil(player_max/2) per team. These are used for first spawn and for respawn (quakec_ctf client.qc:535-547). DM spots are kept, because runes need them.
- Q2 CTF: info_player_team1/2, which are used only for first spawn (ctf/g_ctf.c:449-455). Every later respawn picks an info_player_deathmatch with no team filter, so either team can respawn at any DM spot. Team starts alone therefore cannot keep attackers out of the enemy base. The shared DM spots follow the D32 policy, which recommends spots placed in neutral and midfield zones, balanced between halves and away from both flags. They are measured by the CTF fairness metric in rank 3.
- Q3 CTF: at least ceil(player_max/2) team_CTF_red/blueplayer per team, plus team_CTF_*spawn.
- Q3 gametype, notfree and notteam keys. notfree also removes entities in tourney and SP.
- Q3 bot arena (D15 option a): an `.arena` script (map, bots, longname, fraglimit, type) and info_player_intermission. The map is loaded with `spmap`, because `map` resets g_gametype 2 to FFA (sv_ccmds.c:179-181).
- TDM is stated honestly. Its map contract is DM in Q1 and in Q2 3.19 (teams come from dmflags or server cvars). Q3 GT_TEAM uses DM spawns (g_client.c:1076) and a 30 s weapon respawn. TDM depth therefore means team-scale layout and spawn spread (C3), not new entities.

**Deliverables.**
- GameMode, GameModule, PromptSpec player_min, player_max, compatible_modes and game_module. Exhaustive switches with no `default`, so the compiler finds every site.
- A normalizer that is word-boundary aware, negation aware and follows prompt order.
- Typed `prompt.*` findings (code, span, reason, resolution).
- Validator mode contracts per (game, module, mode): starts, intermission, co-op, team and CTF counts, hull overlap, start-in-solid.
- Entity emission that cites the id source file:line in code comments.
- An optional `<map>.arena` writer that takes its type tokens from arenas.txt.
- `tests/test_prompt_normalization.cpp` implementing PARSE-MATRIX-v1.

**Acceptance tests.**

| Test | Metric | Threshold | Fixed set | Evidence level | Tool |
|---|---|---|---|---|---|
| G2 intent match | Resolved game, primary mode and module equal the target | 100% of the cases for all 17 targets (today 70/138; co-op, TDM and bot arena 0/8; tourney 2/8). A path dropped from the claim still has to resolve correctly. | BENCH-v1 | static analysis | `run.py --gates G2` |
| Draft-v0 mode regressions | The 12 representable misses resolve as expected | 12/12. q2-showcase-hero resolves to quake2/deathmatch, players 2..8, compatible_modes [duel], with an info-level `mode_secondary_compatibility` finding. Because the hero case is sealed, it is checked by the `qmg brief` dry run, which generates no map | 11 BENCH-v1 ids at their recorded seeds, plus the q2-showcase-hero prompt in dry-run mode | static analysis | ctest plus `run.py --filter`; `qmg brief` |
| Parse matrix | Normalization failures | 0 | PARSE-MATRIX-v1 | static analysis | `ctest -R prompt_normalization` |
| G7 strict mode-entity contract | Required entities per mode in the compiled entity lump | 100% of cases. Q1/Q2 info_player_start ≥ 1; intermission ≥ 1; coop starts ≥ player_max − 1; Threewave and Q3 CTF team starts ≥ ceil(player_max/2) per team (today q1/q2 coop 0/8, Threewave 0/8, Q3 CTF 1 per team) | BENCH-v1 | compiler output (BSP entity lump) | `run.py` G7 plus validator CTest |
| R1-s entity safety | Q1 maps with info_intermission; Q1/Q2 MP maps with info_player_start; Q1/Q2 SP maps with ≥ 3 info_player_coop; Q3 CTF team-begin spots per team; large monsters and SP pickups whose hull overlaps a support or wall brush | 100%; 100%; 100%; ≥ ceil(player_max/2); 0 (today gaps 6–7 occur at brutal intensity and in the seed-5 probe). The compiler gate passes for each cause, and each lands as its own baseline-N | BENCH-v1 per game; the audit's brutal-intensity and seed-5 probes | compiler output (entity lump) plus static analysis | run.py G7 subset; validator hull test |
| Spawn legality | Pairwise hull overlap; start-in-solid; navigation binding of spawn-class entities | 0 overlaps (horizontal separation ≥ 64 u for 32-wide hulls); 0 in solid; 100% bound to supported nav nodes | BENCH-v1 co-op, CTF, TDM cases | static analysis | navigation analyzer plus validator |
| Engine acceptance of mode entities | Smoke verdict under each mode profile | 100% of claimed MP cases under their profile: Q2 deathmatch 1, deathmatch 1 + dmflags (TDM), game ctf; Q3 `map` with g_gametype 0, 1, 3, 4, and `spmap` for bot arena. Q2 SP and co-op cases run under coop 1 only, because a dedicated server forces deathmatch 1 without it (D31); they are reported in Phase 3 and gate at the E1 checkpoint. Stock positive controls pass. A no-intermission negative control is flagged by the validator | BENCH-v1 per game, scheduled as H harnesses land | engine initialization | H1 and H2 smoke drivers |
| Spawn-selection and match-end safety | Telefrags at team begin; "no spawn point"; Q1 "Program error" at match end | Q3: 0 telefrags and 0 "no spawn point" with bots to capacity. Q1/Q2: static evidence only, unless a loopback client is approved (D20). In that case 0 "Couldn't find spawn point" and 0 "Program error" on a forced level end. | BENCH-v1 Q3; Q1/Q2 if D20 approves | engine runtime with bots (Q3); static analysis (Q1/Q2 by default) | H2 bot runs; validator |
| Unaffected outputs unchanged | .map bytes, per cause, for every case outside that cause's declared scope. The scope is a list of case ids written into the experiment record before the run | 0 byte differences outside the declared scope. Any out-of-scope difference fails the cause, and the failed record is kept. The cause is then split, or redeclared with a wider scope under a new experiment record | BENCH-v1, MP-PORTFOLIO | static analysis | `compare.py --scope` against baseline-N |
| Normalized determinism key | Case and whitespace variants give the same .map and normalized-brief hash | 10/10 pairs | 10 BENCH-v1 prompts × 2 variants | static analysis | `ctest -R determinism` |

**Depends on.** X1. X2 for the classname tables (can be done in parallel). H1 and H2 for the engine rows. R1-s needs only X1 and the D7/D8 approvals.

**Architectural changes (need approval).** GameMode enumerators, GameModule and the Q3 gametype vocabulary (D4). Report v5 (D2). Determinism key change (D5). Q3 `.arena` output (D22). The R1-s IR roles (intermission, coop_start, team starts) from D8.

**Risks.**
- New precedence changes outputs for existing prompts. Each of R1-a to R1-h is recorded as its own cause with its own baseline-N. No two of them share a commit or a measurement.
- Tests that assert current behaviour must change, with the reason recorded.
- Co-op starts alone do not make co-op claimable. That depends on E and on the co-op output model (D28).
- The Q1 contract choice changes intermission, spawn selection and respawn rules.
- Extra spawn entities change the spawn-safety metrics. Re-baseline and keep the old results.

**Size.** L.

### 9.4 Rank 2 — Honest, expressive brief: typed findings and provenance first, then semantic-plan-v2 and the request trace (A)

**Why it matters to players.** The prompter learns the outcome before any map loads.
- Of 859 expected requests, 419 were honoured, 36 partial, 259 not honoured, 56 not representable and 89 unverified. Zero findings were raised.
- The hero prompt lost 8 of 15 requests (6 more unverified) while the CLI printed `validation=passed`.
- The theme falls back silently to industrial, and has false positives ("database" gives techbase, "no water" gives waterworks).
- rules/v1 sets lighting_style=balanced in 138/138 cases and verticality=50 in 126/138, so "dark", "moody" or "low-key" change nothing.
- No placement qualifier (backside, exposed, far apart) is ever verified.
- C–G need typed input to act on.

**Scope.**

*Stage A1, Phase 3, all games.*
- A DesignBrief stage between PromptParser and SemanticPlanner.
- Typed findings for every unknown, unsupported, clamped, coerced or defaulted request.
- Provenance on every brief field: explicit (with byte span), derived (with rule id) or defaulted.
- Request extraction gives each explicit request an id.
- A RequestTrace stage after analysis links each request id to satisfying layout, entity, brush and material ids. Status is honoured, partial, unmet with a finding, or `pending_analyzer`. `pending_analyzer` names the analyzer and **never counts as honoured**.
- Report v5 is an additive superset of v4: brief, findings, field_provenance, request_trace, unmet list, player range, module, profile and registry hashes, source revision.

*Stage A2, Phase 4 for Q2, later per game.*
- `schemas/semantic-plan-v2.schema.json`, `grammars/semantic-plan-v2.gbnf` and a rules/v2 planner.
- v1 stays readable: the parser accepts schema_version 1 and 2; a deterministic v1-to-v2 upgrade marks new fields defaulted; rules/v1 stays selectable for baseline reproduction.
- Field groups:
  - mode, module and range (from rank 1), and size. **Size** is a band plus an optional room count. The bands are small (4–6 rooms), medium (7–10) and large (11–16), each with a footprint cap that B calibrates on stock maps, provisionally 3072 u, 4096 u and 6144 u on the longer axis. The band comes from size words ("small", "compact", "large", "huge"), from an explicit room count, or, when neither is given, from the player range. A derived band is marked derived. The bands need approval (D3);
  - theme and sub-theme from a per-game vocabulary keyed to F kits, plus **mood**, a closed word list (for example grim, eerie, oppressive, calm, frantic, tense). Each mood maps through a documented rule to lighting intent and a kit sub-theme. A mood word outside the list becomes a typed `unsupported_mood` finding;
  - lighting intent: warm/cold, dark/bright, outdoor, accents;
  - landmarks, with kind (for example furnace, statue, tower, pillar cluster, sky opening), position intent (central, at a decision point, visible from the start) and count. These are built by C1's LandmarkSpec (D8);
  - required and forbidden items with placement intent: role, side, exposure, separation, elevation;
  - liquids, outdoor/sky, floor-level count and mechanisms (lift, teleporter, jump pad, ramp, drop, door);
  - SP difficulty, keys, secrets, progression, and Q2 unit membership.
- Every value is checked against id-source classnames.
- **A field is activated for a game × mode only when its owning workstream can honour it.** Until then it is parsed and reported as "unsupported for <game>/<mode>".
- rules/v2 sets lighting intent, mood, size band, room roles and mechanisms from prompt words with no model.
- CLI overrides: `--theme`, `--lighting`, `--verticality`, `--intensity`, `--loops`, `--players-min`, `--players-max`, `--size`.

*Qualifier verifiers.* They are defined on analyzer metrics now, before any measurement, and never tuned to output. The thresholds below are provisional. B recalibrates each one once, on stock maps, before it gates. Any later change gets a new verifier version, and earlier verdicts are never recomputed under it. Percentiles are taken over the map's own navigation nodes or rooms.
- **Traffic** of a node is the share of all spawn-to-major-item shortest paths that pass through it.
- **backside**: traffic at or below the map's 33rd percentile, and at least 1 room-graph edge off the main circulation loop, which is the highest-traffic cycle.
- **exposed**: the share of eye-height navigation samples with line of sight to the item origin is at or above the map's 67th percentile.
- **contested**: at least 2 node-disjoint approaches, and traffic at or above the 67th percentile.
- **far apart**: path time between the two items at the game's run speed is at least 40% of the map's path diameter.
- **central**: room closeness centrality at or above the 67th percentile.
- **elevated**: floor height at least 64 u above the median floor of adjacent rooms, or on the top floor level.

The metrics come from D, C and G as those land.

**Deliverables.**
- `include/qmg/design_brief.hpp` and `src/design_brief.cpp`.
- The findings type and codes.
- The report v5 writer, with v4-compatible readers updated in tools/benchmark and tools/mp_portfolio.
- The request extractor and trace stage.
- v2 schema, GBNF and rules/v2, with a round-trip of v1 fixtures.
- A per-game activation table (field × game × mode: supported, or unsupported with a finding).
- Grammar-matrix tests (3 games × every claimed mode × the v2 fields, valid and invalid). Opt-in real-GGUF tests with at least 1 prompt per v2 field group per game wherever the group is active (items, mechanisms, landmarks, lighting and mood, size, SP keys and secrets, Q2 units). That is about 18 prompts, frozen as GGUF-v2.
- A README/ROADMAP vocabulary-gap list. ROADMAP:186 claims success "within the bounded vocabulary" without listing the gaps.

**Acceptance tests.**

| Test | Metric | Threshold | Fixed set | Evidence level | Tool |
|---|---|---|---|---|---|
| G9 zero silent drops | Expected requests that are not honoured and lack a matching typed finding | 0 (today 348 of 859, with 0 findings) | BENCH-v1 | static analysis | `run.py --gates G9` |
| No missing report fields | Requests left unverified because the report has no field for them (players, module, lighting intent, floor levels, theme kit) | 0 | BENCH-v1 | static analysis | run.py request evaluator on report v5 |
| G10 conflict surfaced | Contradictory cases with a typed conflict finding of the expected class, both prompt spans and the declared resolution | 34/34, and 100% of new cases (today 0/34) | CONTRA-v1 | static analysis | `run.py --gates G10` |
| Provenance completeness | Brief fields tagged explicit (with span), derived or defaulted | 100% of fields in 100% of reports. Every defaulted theme is flagged. 0 substring false positives on the Phase-1 list | BENCH-v1 plus the PARSE-MATRIX false-positive rows | static analysis | report v5 JSON-schema validation; `tests/test_design_brief.cpp` |
| v1 still readable | v1 plans, fixtures and GGUF transcripts parsed, validated and upgraded; byte reproduction under the legacy profile | 100%. With `--planner rules/v1` and the baseline stage profile, baseline bytes reproduce for every case whose resolved mode is unchanged | Checked-in v1 fixtures, tests/golden, BENCH-v1 | static analysis | `ctest -R semantic_plan`; `compare.py` |
| Grammar matrix | Acceptance of valid and rejection of invalid v2 plans | 100% and 100% | Generated plan matrix (3 games × claimed modes × v2 fields) | static analysis | ctest grammar group plus the tokenizer grammar-cardinality test |
| Opt-in real GGUF | v2 plans from the hash-pinned model pass semantic validation; two-run identity; field agreement with rules/v2 per field group | 100% valid; byte-identical over 2 runs at fixed `n_threads`; agreement reported per field group but not gated, since a 135M model is not expected to match rules/v2. A missing or invalid group gives a typed planner finding, never a silent fill | GGUF-v2 (about 18 prompts covering every active v2 field group in each game), SmolLM2-135M `c4a3dd03…` | model inference | `ctest -L gguf` |
| rules/v2 responds without a model | Prompts whose lighting, mood, verticality, size and role words are reflected in the plan | 100% of prompts containing those words (today balanced 138/138, verticality 50 in 126/138). Every mood word on the list changes lighting intent or sub-theme as its rule says, and words outside the list give `unsupported_mood` | BENCH-v1 plus PARSE-MATRIX mood and size rows | static analysis | `run.py semantic_plan_fields` |
| Size honoured | Size requests resolved to a band; generated room count and footprint inside that band | 100% resolved, with provenance; 100% inside the band, or a typed finding if the band clashes with the player range | BENCH-v1 size cases | static analysis | report v5 brief plus layout bounds |
| Forbidden items absent | Compiled entities of a forbidden classname or class ("no BFG", "no powerups"); forbidden requests traced | 0 instances; 100% traced as honoured | BENCH-v1 forbidden-item cases (at least 3 per game) | compiler output (entity lump) | run.py entity tally against the trace |
| Qualifier verifier controls | Classification of paired fixtures (railgun backside against frontside; Quad exposed against hidden; Quad contested against single-approach; BA/MH far against near; landmark central against peripheral; deck elevated against floor) under the numeric thresholds above | 100% | `tests/fixtures/qualifiers` | static analysis | `ctest -R request_trace` |
| Qualifier verdicts | Requests carrying a qualifier that receive a measured verdict | 100%. No "partial by default"; `pending_analyzer` only where the analyzer is not built yet | BENCH-v1 qualifier cases, HEROES | static analysis | trace stage with D/C/G analyzers |
| Hero vocabulary dry run | Hero requests mapped to brief fields, not unsupported findings | 15/15, checked statically. The dry run generates no layout or map at any seed, so the seal holds | q2-showcase-hero prompt (sealed case) | static analysis | `qmg brief --prompt …` (dry run) in CTest |
| Honour rate | Representable specific requests honoured on claimed paths; hero requests honoured | ≥ 85% at RC (provisional, D11(5)). Heroes: 100% of the requests not declared out of scope in HEROES before generation; for the Q2 DM hero that is 15/15, none declared. A hero miss fails that hero's exit: the cause is fixed and the same seed rerun, and the failed attempt is kept (D11(5)) | BENCH-v1 claimed specific cases; HEROES | static analysis | run.py summary |

**Depends on.** X1, rank 1. Its qualifier verdicts depend on D, C and G.

**Architectural changes (need approval).** semantic-plan-v2, GBNF and rules/v2 with v1 still readable (D3). The DesignBrief and RequestTrace stages (D6). Report v5 (D2). CLI overrides.

**Risks.**
- The v2 grammar may exceed what the 135M model can follow. rules/v2 stays authoritative. A model failure gives a typed planner finding and is never filled silently.
- Vocabulary may run ahead of construction. The activation table and "unsupported" findings guard against that.
- GBNF size and token limits.
- Schema versions are bumped at most once per phase.

**Size.** XL.

### 9.5 Rank 3 — Items, spawns and economy (D)

**Why it matters to players.** This is what players meet most often. Evidence is compiler-output entity tallies plus the static visibility analyzer.
- No map in any game has ammo, Mega Health, armor tiers or powerups.
- Weapons:
  - The railgun is absent in 22/30 Q2 arena maps and the plasmagun in 30/40 Q3 maps.
  - 23/26 Q1 arena maps repeat a weapon, and 3 Q2 maps have no RL.
  - Q3 spends a slot on the machinegun players spawn with. The Q1 SSG, Q2 SSG/BFG/grenades and the Q3 BFG are missing.
- Armor: 35/40 Q3 maps carry 3 or more red armors.
- Requests: 47 of 53 major-item requests are ignored.
- Spawn safety by analyzer: Q1 DM 20/76, Q2 DM 55/108, Q3 DM 56/158, Q3 CTF 25/82, Q2 CTF 10/140. Only Q2 duel reaches 16/16. Duel gets 2 spawns against a median of 7 in the Q2 archive duel cohort (docs/Q2_FIRST_STUDY.md:24). No Q1 or Q3 reference has been measured. Q1 and Q3 LOS are sampled at eye offset 0 (visibility.cpp:278-282).
- CTF item sets are not mirrored, and the blue half gets the stronger weapons and twice the armor. Each flag room holds a weapon about 156 units from the flag, and team spawns can sit next to the enemy flag.
- SP has no ammo or armor.

**Scope.** Work is sequenced one cause at a time, and each step is measured: catalogue, then budget, then solver, then spawn policy, then CTF. The catalogue (D-a) lands in Phase 3 for all three games, because it needs only X2 and D8. The budget, solver and spawn policy follow in Phase 4: Q2 DM/duel first for the hero, then Q3, then Q1 if kept.
1. **Catalogue and IR (D-a), Phase 3, all games.**
   - New PlacementRoles: ammo, powerup, holdable, mega_health, and armor and health tiers as variants.
   - New Placement fields: tier, spawnflags, wait, random, gametype, notfree, notteam, notbot, team.
   - Default-inventory weapons are excluded with a reason (Q2 blaster, Q3 gauntlet and machinegun).
   - Q2 SpawnItem zeroes non-skill item spawnflags (g_items.c:1065-1072), so nothing relies on them.
   - The existing allocator is extended, as separate causes: the missing weapons (Q1/Q2 SSG, Q2 BFG and grenades, Q3 BFG) join its rotation; every placed weapon gets matching ammo; Mega Health and the armor tiers are placed; the Q3 machinegun slot is dropped. Placement by role waits for the solver.
2. **Per-mode budget** per game × mode × size band, in the profiles. Calibrated on B cohorts as descriptive bands (D30).
3. **Role solver (D-b)**, an ItemEconomy stage over the layout graph using navigation and visibility. It replaces round-robin allocation on migrated paths. Items it cannot place become typed findings.
4. **Item-timing simulation (M5).** It uses per-game speeds (Q1 320, Q2 300, Q3 320) and the profile timers. It honours per-entity `wait` and `random`, and starts Q3 powerups at their first-spawn window of 30–60 s after map start (g_items.c:696), with every other item present at t = 0. It measures:
   - major-item cycle time, the shortest closed tour of every major item at run speed;
   - how many majors one position can cover within their respawn windows;
   - the losing player's recovery path, as a route and as a **path length** in seconds.

   It replaces the saturated item features in the ranker. Weights stay hand-set (D30).
5. **Spawn policy (D-c), every game × mode.**
   - Count from the player range. Q2 duel: the Q2 archive duel cohort has a median of 7 DM spawns (docs/Q2_FIRST_STUDY.md:24, 50 independent groups), and B measures its p10–p90 band; the gate is ≥ 5 until then, provisional. Q1 and Q3 duel and tourney have no calibration yet: B derives their bands from dm1–dm6 and q3tourney1–6 plus pro-q3tourney2/4 before the gate is hard. The earlier "5–9" band had no recorded source and is withdrawn. DM: at least player_max + 2. This is a project choice with no stock basis yet. For comparison, the Q2 archive has medians of 7 for small DM and 8 for medium DM (Q2_FIRST_STUDY.md:25-26).
   - Spread: spawns in at least ceil(count / 2) distinct rooms (provisional).
   - Facing derived from the free sightline.
   - No LOS to another spawn or to a top-tier item at the game's eye height.
   - Not within 256 u path of a power item.
   - Refinement extended beyond compact Q2 rooms 2–8.
   - Analysis aware of each engine's spawn selection: 1996 Q1 sequential with a 32-unit skip; QW random with 84; re-release random and not visible with 384; Q2 and Q3 as in source.
6. **CTF (D-d)**, with C3: mirrored item multisets, no weapon in flag rooms, team starts in their own base, and shared DM respawn spots placed under the D32 policy, because every Q2 CTF respawn after the first uses team-agnostic DM spots (ctf/g_ctf.c:449-455). Threewave uses team spawns for respawns too, and falls back to DM spots only when no team spot exists; runes also need DM spots (quakec_ctf client.qc:535-547).

**Before any contract uses a design-history claim, the claim is measured.** The brief says q2dm1's armor, health and top weapons cluster near one ledge, and that dm3 has a power side (RL + Quad) and a recovery side (RA + SNG). B1 first measures item clustering in q2dm1 and item sides in dm3 from their stock entity lumps, using the metric definitions of D-c1 and D-c3, and records the result. If the measurement does not confirm the claim, the card records it as unconfirmed, and that map is not used as a control for that contract. This keeps the controls from being circular.

**Generator contracts.** Each one is a provisional hypothesis with its counterexample, calibrated by B before any status change.
- **D-c1. Weapons are kept apart from the health and armor that support them.** Counterexample: q2dm1 is expected to fail this check, as a negative control, but only if B1's measurement confirms its clustering. A deliberate, exposed high-risk stack with 2 or more exits can be legitimate.
- **D-c2. A backside power weapon is reachable, quiet, elevated and has two exits.** Counterexample: in compact duel maps "quiet" can become a dead end, and elevation can become a camping perch, so D-c6 applies.
- **D-c3. DM3 power side (RL + Quad) against recovery side (RA + SNG).** The sides are taken from dm3's measured entities, not from the brief alone. Counterexample: sides collapse in duel and very small maps. DM3's own RL+Quad pairing also contradicts D-c4.
- **D-c4. FFA keeps the Quad far from the RLs.** Counterexample: DM3.
- **D-c5. Q3 duel RA/MH/YA trade-off (Q2 duel: BA/MH), so that holding one means giving up another.** Counterexample: too much separation turns into item running with few fights, so control windows must overlap with travel times. Calibrate on q3dm6 against pro-q3dm6 and q3tourney4 against pro-q3tourney4.
- **D-c6. No camping spot (at most 1 exit, plus cover) overlooks a key item and its only approach.** Counterexample: a power position must overlook something, so require it to be exposed with 2 or more exits rather than banning it.
- **D-c7. Spawn rules.** Counterexample: zero spawn-pair LOS does not prove respawns are safe (playbook), and over-strict separation pushes spawns into dead ends.
- **D-c8. By default, power items (Quad, Pent/Invulnerability, the top armor in FFA) sit in contested, exposed spots,** judged by the qualifier verifiers even when the prompt does not ask. Counterexample: in duel maps the top armor is a control item, and full exposure lets one player deny it from a safe distance. In duel and tourney the contract is relaxed to contested only.
- **D-c9. By default, recovery items (health and lower-tier armor) sit on the backside, and in Q2 so does the armor, as the brief's Edge fixes ask.** Counterexample: if every recovery item is on the backside, the losing player must cross the whole map. The recovery-route length limit in the item-timing test bounds this.
- **D-c10 (DM2). A committed single-route power position has a counter:** an exposure from at least 1 other position, or a hazard that the opponent can trigger. Hypothesis only; the dm2 mechanism is measured in stock entities first (B2). Counterexample: a counter that is always lethal makes the item a trap nobody takes.

**Deliverables.**
- Item catalogue data (in X2) and IR fields, with validator, map-builder and report support.
- Budget tables with stock calibration records.
- `src/item_economy.cpp` behind a stage switch.
- The timing simulator (`tools/item_timing` plus a quality-analyzer section `item_economy`).
- The spawn policy, generalized from spawn_refinement.cpp, with eye heights cited from source.
- Qualifier analyzers (backside, exposed, contested, far apart, central, elevated) with the rank-2 thresholds, for A's trace and for D-c8/D-c9.
- A CTF validator contract, `ctf_mirror`.
- Item-control analysis in each hero study.

**Acceptance tests.**

| Test | Metric | Threshold | Fixed set | Evidence level | Tool |
|---|---|---|---|---|---|
| Catalogue matches the id tables | Pickupable classnames missing per game and module; timers matching the facts fixture with a citation | 0 missing (exclusions listed with reasons); 100% matching and cited. The doc keeps the brief-against-source discrepancies | Extracted Q2 itemlist (g_items.c), Q3 bg_itemlist (bg_misc.c), Q1 progs/QuakeC classnames; facts fixture | static analysis | `ctest -R item_catalogue`, `-R item_timers` |
| IR round trip and compiler gate | Item keys surviving validate, build and parse; compiled acceptance | 100% survive; the Q3 entity token is `tournament`; compiler fixtures pass for all three games (serialization change) | CTEST-FIXTURES per game | compiler output | `ctest -R item_ir` plus the compiler gates |
| Item-set completeness | Budgeted tiers present; matching ammo for every placed weapon; no weapon duplicated before every budgeted weapon is placed; major counts within budget | 100% of MP cases at or above the budget's minimum room count, with a typed finding below it. Q3 duel RA ≤ 1, MH ≤ 1, Quad ≤ 1. Today: RG absent 22/30 Q2, PG absent 30/40 Q3, 3+ RA in 35/40 Q3, repeated weapons in 23/26 Q1, ammo 0 everywhere | BENCH-v1 MP cases plus the item-specific cases | compiler output (entity lump) plus static analysis | run.py entity tally against the budget table |
| Item requests honoured | Major-item requests honoured with a measured qualifier | ≥ 90% honoured, the rest typed unmet, 0 silent (today 47/53 ignored) | BENCH-v1 plus the item cases | static analysis | trace stage (rank 2) plus D analyzers |
| Spawn safety | Analyzer-safe share; spawn–spawn LOS pairs; spawn–top-tier LOS; spawns within 256 u path of power items; clear distance along facing; spawn count; distinct spawn rooms | ≥ 90% safe per game × mode, with no regression from Q2 duel 16/16; 0 spawn–spawn LOS; 0 spawn–top-tier LOS; 0 near power items; 100% with ≥ 192 u clear (provisional); Q2 duel count ≥ 5 until B's duel band exists, then inside it; Q1 and Q3 duel and tourney inside the band B derives, with the row reported until then; DM ≥ player_max + 2; spawns in ≥ ceil(count/2) rooms | BENCH-v1 at player_min and at player_max | static analysis | visibility analyzer at the profile eye height |
| Contracts discriminate stock | D-c1 to D-c6 and D-c10 verdicts on stock controls and generated maps | The design-history claims are measured first (q2dm1 clustering, dm3 sides). If q2dm1's clustering is confirmed, q2dm1 **fails** D-c1/D-c2; if it then passes, the metric is wrong and is not promoted. Positive and negative stock controls are recorded from the B calibration before any generated map is measured. Generated maps pass 100%, with waivers within the 5% cap (8.3) | STOCK-Q2 q2dm1–8; STOCK-Q3 q3dm6, q3tourney2/4 and the pro- revisions; STOCK-Q1 dm2–dm6 as each B reader lands; BENCH-v1 | static analysis | D analyzers through map_study and qmg quality |
| Default item roles | D-c8: power items contested and exposed (contested only in duel/tourney); D-c9: recovery items and, in Q2, the armor on the backside, on prompts that do not ask for placement | ≥ 90% of instances per path (provisional), the rest typed findings; the stock rate is recorded first on q2dm1–8, q3dm6 and q3tourney2/4 | BENCH-v1 vague MP cases, MP-PORTFOLIO | static analysis | qualifier verifiers |
| Item-timing control | Coverage of majors from one hold position; major cycle time; recovery route and its length from every spawn to weapon plus armor/MH that avoids the hold's LOS; FFA Quad-to-nearest-RL path distance | Q2 duel: no hold position covers both BA and MH within their windows. Q3 tourney: none covers more than 1 of {RA, MH} or more than 2 of {RA, YA, MH}. Recovery: a route exists for 100% of spawns, and its path time is ≤ 1.5 × the unconstrained shortest route and ≤ the respawn window of the held major (Q2 BA 20 s, Q3 RA 25 s). Cycle time: inside the matching stock cohort's p10–p90 band; reported but not gated until B delivers that band, then gated at the path's depth exit. FFA Quad–RL path ≥ 40% of path diameter. All provisional and predeclared before measurement | BENCH-v1 DM/duel/tourney/FFA cases, SPAN-PORTFOLIO, HEROES, STOCK-Q3 tourney maps | static analysis (simulation) | `tools/item_timing` over the navigation graph |
| CTF fairness | Item multiset per team; mirror position error; weapons within 384 u path of a flag; team starts in their own base; shared DM respawn spots, which either team uses after first spawn in Q2 CTF (Threewave uses them only as a fallback and for runes) | Identical multisets; ≤ 16 u under the declared transform; 0 weapons; 100% of team starts. Shared DM spots: every spot ≥ 0.3 × the flag-to-flag path length from **both** flags; halves balanced within 1 spot; mean path time to each flag equal within 10%. Provisional until measured on q2ctf1–5 | BENCH-v1 CTF targets (Q2, Q3; Q1 only if kept) | static analysis | `ctf_mirror` validator (needs C3) |
| Runtime item initialization | Items starting in solid or dropped at spawn | 0 on every case of each game; a buried-item control is rejected | BENCH-v1 per game (Q2 first, Q1/Q3 as harnesses land) | engine initialization | H smoke drivers |
| Hero item routes | Native traversal of spawn to RG (backside), spawn to Quad, BA to MH, and the losing player's recovery route, as predeclared in HEROES | 100% complete. A failure fails the hero's exit; the cause is fixed and the same seed rerun. A failure blamed on the harness still counts until a harness fix passes the stock control routes. Every failure stays in the study | HEROES (Q2 DM first; Q1/Q3 heroes once H4 exists) | native movement trial | `tools/native_movement` (Q2 3.19 Pmove); H4 harnesses |

**Depends on.** D-a: X1 (including the repair fix), X2 and D8 only. The solver, qualifiers and spawn policy also need rank 1, rank 2 and B1 for calibration. C3 for the CTF mirror. H for the runtime rows.

**Architectural changes (need approval).** New PlacementRoles and Placement key fields (D8). The ItemEconomy stage replacing round-robin (D6). Timing simulation feeding the ranker (D23). Spawn policy for every game and mode.

**Risks.**
- Item counts change every baseline. Measure each cause separately.
- Thresholds without stock calibration are guesses and are labelled provisional.
- The Q1 progs contract (D13) changes timer gating.
- Small room counts cannot satisfy every budget. These become findings, not silent drops.

**Size.** L.

### 9.6 Rank 4 — Theme and materials: visible defects first, then per-game kits and building rules (F)

**Why it matters to players.** Players see this on load. Evidence is compiler output plus static image and asset scans.
- Q1 maps render 97.6% of faces with names that exist in no id asset (WALL1_1, FLOOR0_1), and no `wad` key is written. 48/48 qbsp logs say "unable to find texture". The gates hide this with `-notex`.
- Every Q2 ceiling is e1u1/ceil1_4, which inherits SURF_LIGHT 10000. One measured map has a mean lightmap of 152 with this, against 20.2 with explicit metadata.
- Q2 walls are near-black flat1_1, mean RGB (17.9, 14.1, 9.8), on about 70% of faces.
- Q3 walls are the near-black blend layer comp3text, mean RGB (3.2, 0.9, 0.5).
- Theme has no visual effect: there are 4 materials per game across 11 theme labels.
- 0/44 liquid and 0/16 sky requests are met, and Q3 `qmg/*` liquids would compile solid.
- Hidden Q1/Q2 faces get the wall texture instead of skip.
- Tests assert the broken names (tests/test_main.cpp:774), so ctest stays green.

**Scope.**

*F1, Phase 3: visible defects.* Each item below is one cause, measured on its own.
- Explicit Q2 face metadata (flags, contents, value) on every face. This is a serialization change and goes through the compiler gate. **The resulting darkening of Q2 maps and the change in light time are recorded as their own result before G lands.**
- Replace the Q2 lamp-tile ceiling and the near-black wall roles. Replace the Q3 comp3text wall and the pewter_shiney ceiling fill.
- **Default flip of the Q2 emitter removal (D33, recommended option b).** The explicit metadata and the ceiling replacement land and are measured behind their switches in Phase 3. Their defaults flip in Phase 4, in the commit just before the Q2 light rescaling (G1) flips. Each flip is still measured as its own cause. Default Q2 output therefore never ships a whole phase below the stock band (mean about 20 against 45–73). The Phase-3 exit measures these rows with the F1 profile switched on.
- skip (Q1/Q2) or caulk (Q3) on every hidden face, including non-shell solids. Face exposure comes from the shell's exposed-face data.
- Q1 `--wad`: record path and sha256; write worldspawn `wad`/`_wad`; validate names against the WAD lumps, per D9.
- Liquids: an original `scripts/qmg.shader` with origin, licence, author and hash, or a mapping to verified stock shaders. Q2 liquids use real WALs with WARP and TRANS kept.
- Update the tests that assert broken names, and record the old names as counterexamples.

*F2, Phase 4 for Q2, Phase 5 for Q3, Phase 6 for Q1: palette kits as data.*
- Kits: Q1 base, medieval, metal, runic; Q2 unit themes, including a Strogg refinery/industrial kit and a canyon/outdoor kit; Q3 gothic, base/tech, space.
- Roles: wall, floor, ceiling, trim/border, accent/landmark, riser, light-emitting, hazard, water/slime/lava, sky, skip/caulk/clip/hint/trigger.
- A MaterialResolver keyed by kit, room role, height band and seed replaces `material_for`.
- Theme and sub-theme select the kit. An unknown theme raises a typed finding and is marked defaulted.
- Sky materials plus worldspawn sky keys per game.
- A materials table in the report: role, face count, metadata, hash.

**Building-rule contracts.** Each is checked statically, and the counterexamples are kept (Romero rules).
- **F-c1. Change floor texture exactly where floor height changes.** Counterexamples: continuous ramps and stair runs where texture continuity reads as one route; same-height changes that carry meaning (CTF team floors, hazard edges, landmark inlays), tagged as exceptions. Measure the stock violation rate first.
- **F-c2. Trims at doorways and at borders between wall segments.** A wall-segment border is a vertical edge where two wall faces meet at a corner, or where the wall material, wall plane or floor height changes. Counterexamples: organic, cave and runic walls; trim spam on short segments; Q3 curves. The added face count and VIS time are measured.
- **F-c3. Strict alignment fitted to texture size.** Measured as: (a) on axial faces, the texture offset lines up the texture grid with the world grid at the texture's size, or matches the coplanar neighbour; (b) across each seam between coplanar faces of the same material, the texture continues with no jump. Counterexample: deliberate rotation as a cue (grates, hazard stripes), tagged in the IR. Q3 patches need per-surface fitting and are measured separately.
- **F-c4. Landmark accents at chosen decision points,** applied to C's landmark geometry (LandmarkSpec), not to material alone. Counterexamples: too many accents dilute the cue, and mirrored CTF bases need distinct identities, not identical materials.
- **F-c5. Hidden faces use skip/caulk.** Counterexample: faces visible only through liquids, translucent surfaces or detail gaps must not be culled.

**Deliverables.**
- F1 fixes, each with a baseline delta record.
- The material admission stage (using X2).
- `data/palettes/<game>/<kit>.json`, with a completeness test for every kit the brief names.
- The original Q3 liquid shader (and later a botclip shader for C2) with provenance.
- Q1 `--wad`, plus a Q1 compile gate without `-notex`.
- A static building-rule checker (`tools/material_rules`).
- Compiler gates against real assets.

**Acceptance tests.**

| Test | Metric | Threshold | Fixed set | Evidence level | Tool |
|---|---|---|---|---|---|
| G12 missing textures | "unable to find texture" and missing-shader lines when compiled against real assets (qbsp's own skip lookup is resolved by a real skip texture) | 0 on 100% of cases per game (today 48/48 Q1 maps). Q1 depends on D9 | BENCH-v1 plus CTEST-FIXTURES | compiler output | run.py log parse; material CTest gate without `-notex` |
| No accidental emitters | Compiled texinfo faces with SURF_LIGHT or a value the light schedule did not request | 0 (today every Q2 map, 32 to 1052 surface-light points). At the Phase-3 exit this is measured with the F1 profile on; from the Phase-4 D33 flip onwards it is measured on default output | BENCH-v1 Q2 cases, MP-PORTFOLIO | compiler output | texinfo parse in run.py plus a CTest assertion |
| Kit completeness | Named kits present per game; roles filled per kit | Every kit the brief names exists: Q1 base, medieval, metal, runic; Q2 unit themes including the refinery/industrial and canyon/outdoor kits; Q3 gothic, base/tech, space. In each kit, 100% of the roles are filled with admitted stock names: wall, floor, ceiling, trim/border, accent, riser, light-emitting, hazard, liquids the game supports, sky, and skip/caulk/clip. Where a role cannot be filled from local data, the gap is a typed finding and the kit is not claimed | `data/palettes/**` plus the X2 registry | static analysis | `ctest -R palette_completeness` |
| F1 Q2 effect record | Mean lightmap, floor luminance distribution and light wall time before and after explicit metadata | Recorded per case as its own experiment; no threshold (expected darkening, about 152 to about 20 on one map) | BENCH-v1 Q2 cases | compiler output | lightmap probe (G1) plus stage timers |
| Role admission | Palette entries whose metadata contradicts their role; negative controls | 0; all X2 controls rejected | All kits plus the control list | static analysis | `ctest -R material_admission` |
| Visible-surface luminance | Mean image luminance of architectural wall and floor materials | At or above the 10th percentile of the most-used stock wall/floor textures for that game, recorded by a calibration run before measuring. Interim floor ≥ 40/255; flat1_1 (about 14) and comp3text (about 1.5) fail | All kits plus STOCK-Q1/Q2/Q3 texture usage | static analysis | image probe over WAL, WAD and Q3 images |
| Hidden faces | Non-exposed faces carrying a visible texture | 0, non-shell solids included | BENCH-v1 plus CTEST-FIXTURES | static analysis | .map face-exposure check |
| Liquids compile as liquids | Compiled leaf and brush contents of liquid brushes | 100% water/slime/lava as intended; 0 solid | Liquid fixtures per game; C1 cases later | compiler output | BSP contents check in CTest |
| Theme has a visual effect | Visible roles that differ between kits; theme requests judged by the selected kit, not the label | At least 4 of 6 visible roles (wall, floor, ceiling, trim, accent, light) differ between any two kits of a game; ≥ 95% of visible faces from the chosen kit; 100% of theme requests select the matching kit (today 41 "honoured" by label alone) | 11 theme labels × 3 games at a fixed seed, plus BENCH-v1 | static analysis | run.py material evaluator |
| Building rules | F-c1 coincidence rate; doorway trim coverage; wall-segment border trim coverage; riser material use | F-c1 ≥ 95% (the rest tagged exceptions within the 5% waiver cap) and at or below the stock median violation rate; 100% of doorways trimmed; ≥ 90% of wall-segment borders trimmed in kits that declare trims (organic, cave and runic kits exempt by declaration); 100% of risers use the riser/trim role. Stock rates for the border and F-c1 rules are recorded first | BENCH-v1 claimed cases plus STOCK calibration | static analysis | `tools/material_rules` (CTest) |
| Texture alignment (F-c3) | Axial faces aligned to the world grid at texture size, or to their coplanar neighbour; seams between coplanar same-material faces with an offset jump | ≥ 95% aligned (tagged rotated cues excluded); 0 seam jumps on generated brushes. Q3 patches are reported separately. The stock rate is measured first on dm3, q2dm1 and q3dm6, read directly from the BSP texinfo vectors and offsets with no decompile; only the aggregate rate is recorded | BENCH-v1 claimed cases plus STOCK calibration | static analysis | `tools/material_rules --alignment` |
| Sky and liquid requests | Requests honoured or typed unmet | 100% (today 0/16 sky, 0/44 liquid) | BENCH-v1 sky and liquid cases | compiler output | run.py |
| Rendered appearance | Windowed engine screenshots of each hero's predeclared views (landmark and key areas, listed in HEROES) | Pass/fail: 100% of the predeclared views captured by the engine's `screenshot` command, each with its image hash, engine hash and cfg. The user's ratings are recorded, or null if not observed; they are reported, not a pass criterion | HEROES | engine rendering (capture); review is reported separately | engine `screenshot` command per D19 |

**Depends on.** X1, X2, and rank 2 (theme fields for F2). The user's WAD for Q1. C for sky volumes.

**Architectural changes (need approval).**
- The MaterialResolver and admission stage, with kits as data (D6).
- Extended MaterialRole values.
- Always-explicit Q2 face metadata, a serialization change.
- The `--wad` option and worldspawn keys.
- A shipped original Q3 shader script (D9).

**Risks.**
- The F1 Q2 change drops brightness sharply until G lands. The dark interim state is recorded, not hidden. Under D33 (b) it stays behind its switch until the Q2 rescaling is ready. Under D33 (a) it would be the default for all of Phase 3.
- Without a Q1 WAD, every Q1 path is blocked. A WAD extracted from id1 BSPs (D9(2), second option) departs from the brief, which says the user supplies the WAD. It would need the user's explicit approval plus a record of origin, licence and hash, and it would never be packaged.
- Q1 BSPs embed texture pixels, which is a packaging question (D9).
- The local baseq2 is partial (loose files, no pak), which limits kit choice.

**Size.** L.

### 9.7 Rank 5 — Lighting: compiled readability, visible sources, sun and sky, route lighting (G)

**Why it matters to players.** Players cannot use light to find routes, items or opponents. Evidence is compiler output from a lightmap probe calibrated on stock maps with the same parser (stored bytes, not a render).
- Compiled Q1 floors are near-black: median floor mean 6.6/255, with 89–98.6% of floor luxels below 16. Stock id1 maps sit at 46–95 and 4.5–20.1%.
- Q3 floors: a median 65% of luxels are below 16, against 6.6–19.7% in stock.
- Every Q2 map is a uniform saturated orange at about 174/255 from the accidental emitters. Stock q2dm1–8 and base1 sit at 45–73.
- No compile has sun or sky (98/98 ericw logs, 40/40 q3map2 logs).
- 11 unlit_critical_placement errors were found. One delivered span-portfolio map (seed733) ships with a lighting ERROR.
- The analyzer calls Q1 92.6% readable, and compiled output contradicts it.

**Scope.**

*G1: compiled readability, probe starting in Phase 3 in report-only mode.*
- A per-compiler analytical model: ericw linear falloff with delay/wait; q3map2 inverse-square with pointScale 7500; surface and sun emitters; a bounce approximation.
- Samples on walkable floors and decks, with solid containment and the eye offset, and an occluder policy aligned with visibility (visibility.cpp:430).
- A compiled-lightmap gate that parses BSP29, IBSP38 and IBSP46 lightmaps and the Q3 lightgrid at navigation sample points. The Phase-1 scoreG probe is promoted into `tools/benchmark` (or `tools/lighting_probe`) with parser tests.
- Light placement and intensity scaled to room volume and height.
- Lighting findings feed selection and repair: a candidate with a lighting ERROR is rejected, and a repair proposer adds or moves lights. **No map with a lighting ERROR is delivered.**

*G2: visible sources, sun and sky, route lighting.* A LightSchedule stage runs after the palette.
- Fixtures: Q1 light_torch_small_walltorch, light_flame_* and light_fluoro (classnames verified against the chosen progs); fixture brushes with ericw `_surface`; Q2 light textures with explicit SURF_LIGHT and value; Q3 stock shaders that declare q3map_surfacelight.
- Theme picks the colour.
- Sun and sky: ericw `_sunlight`, `_sun_mangle`, `_sunlight2`, `_sunlight2_color` with SURF_SKY; Q3 q3map_sun/q3map_skylight in a verified sky shader.
- A per-game support table for `_dirt`, `_phong`, bounce and light styles (styles used only where switchable lights serve SP). Each entry cites the pinned compiler version and is confirmed against that build's own log output before use. The candidates are ericw-tools worldspawn `_dirt` and `_bounce` and per-brush-entity `_phong`, and q3map2 `-dirty`, `-bounce` and shader shade-angle keys. Wherever the table says "supported", the keys are written by default and the compile log must acknowledge them.
- Route lighting: entrances, decision points, power items, spawns and flags lit above threshold; a contrast schedule between connected spaces. A **decision point** is a navigation node where 3 or more room-graph edges meet, or the first node inside a room entrance from which 2 or more onward routes diverge. Motif lights follow the plan.
- Remaining analytical warnings are cleared, or each is justified in the study.

**Generator contracts, each with its counterexample.**
- **G-c1. Contrast between connected spaces.** Counterexample: too much contrast at a doorway hides players. In the hero sets, 1254 of 1396 lighting findings are contrast findings. The audit attributes most of them to sampling defects, such as samples not tested against solids and missed raised decks (audit gap 43, static analysis). Whether the same holds for the benchmark's 685 `excessive_connection_contrast` findings has not been measured. G1 re-samples them before any contrast threshold is set. Measure q3dm6 against pro-q3dm6 before promotion.
- **G-c2. No pitch-black MP play space.** Counterexample: SP ambush pockets may be dark when hinted. They need a typed reason and are never on a spawn, key or exit.
- **G-c3. Light comes from visible sources.** Counterexample: stock maps use fill light (`_minlight`) against black corners, so fill is recorded separately with a reason and never hidden.
- **G-c4. Light entrances, decision points and power items.** Counterexample: lighting everything makes nothing stand out, and a dark approach to a power item can be a deliberate risk cue.

**Deliverables.**
- The calibrated LightingAnalyzer and sample policy.
- The compiled-lightmap probe and G13 gate, with stock calibration records for each game.
- A fixture and emitter library per game, each item verified against local data with its hash.
- The LightSchedule stage and a light schedule in report v5.
- Sun and sky keys.
- Lighting findings consumed by selection and repair.
- A light-schedule section in every hero study.

**Acceptance tests.**

| Test | Metric | Threshold | Fixed set | Evidence level | Tool |
|---|---|---|---|---|---|
| G13 compiled readability | Per map: share of floor luxels below 16/255, floor mean, share at or above 250, at navigation-sampled floor points | Inside the per-game stock band on 100% of MP cases. Initial bands are the minimum and maximum over the calibration maps in the Phase-1 probe (scoreG/calib.py, rerun 2026-10-15). Q1, dm1–dm6: dark share ≤ 20.1% (dm2), floor mean 46.1–95.3, saturated ≤ 18.0% (dm2). Q2, q2dm1–8 and base1: dark share ≤ 25.5% (q2dm3), mean 45.2–72.7, saturated 0% in every map, so the interim limit is ≤ 1%. Q3, q3dm1, q3dm6, q3dm17, q3tourney2, q3tourney4 and q3ctf4: dark share ≤ 19.7% (q3dm17), mean 48.5–109.6 in stored bytes, saturated ≤ 23.9% (q3tourney2). B re-derives the bands from the frozen calibration run, and they are predeclared before the gate is made hard. SP bands come with E | BENCH-v1 plus STOCK-Q1 (id1 dm1–dm6), STOCK-Q2 (q2dm1–8, base1), STOCK-Q3 (q3dm1, q3dm6, q3dm17, q3tourney2, q3tourney4, q3ctf4) | compiler output (stored lightmap bytes, not a render) | lightmap probe in run.py, plus a CTest group |
| No uniform saturation | Floor luminance p90 − p10; share at the dominant colour | Inside the stock band. The current Q2 pattern (uniform 174, RGB 255,163,41) is rejected as a negative control | BENCH-v1 plus STOCK | compiler output | lightmap probe |
| Critical placements lit | Compiled brightness at spawns, flags, power items and SP keys/exits; unlit_critical_placement count | 100% at or above the per-game floor, set at the stock p10 of item-location brightness; 0 errors (today 11). The span seed733 map is kept as a recorded counterexample | BENCH-v1, SPAN-PORTFOLIO, HEROES | compiler output plus static analysis | lightmap probe at placement origins plus the analyzer |
| Analyzer agrees with compiler | Share of points where the analyzer's readable verdict matches compiled brightness ≥ 16/255 | ≥ 90% of all samples and ≥ 95% of critical placements, per game (today the Q1 analyzer says 92.6% readable while most compiled floors are below 16) | BENCH-v1 | static analysis compared with compiler output | joined sample table in run.py |
| Visible sources | Point lights within 64 u of a fixture brush, fixture model or emissive face; fill keys listed | 100% attached or recorded as fill with a reason | BENCH-v1 plus HEROES | static analysis | light-schedule validator |
| Sun and sky | Outdoor and sky requests whose logs show at least 1 sun/sky light and > 0 SURF_SKY or sky-shader faces | 100% (today 0/16; "0 suns" in 98/98 ericw logs; "0 sun/sky lights" in 40/40 q3map2 logs) | BENCH-v1 outdoor cases plus the Q2 hero | compiler output | run.py log parse |
| Lighting intent | Lighting and mood requests traced to the schedule and measured in compiled colour and brightness | 100% honoured or typed unmet. Hero: furnace interior median R/B ≥ 1.2, sky-lit exterior R/B ≤ 0.9, predeclared | BENCH-v1 lighting- and mood-request cases plus HEROES | compiler output | lightmap probe by region plus the trace |
| Route lighting | Entrances, decision points (as defined in G2) and power items compared with the compiled room median | ≥ 1.2 × the median in ≥ 90% of instances | BENCH-v1 claimed MP cases | compiler output | lightmap probe |
| Contrast between spaces (G-c1, Romero) | Ratio of the compiled median floor brightness between adjacent spaces, after G1 re-sampling | At least 1 adjacent pair per map with a ratio ≥ 1.5 (contrast present); 0 pairs with a ratio ≥ 4 across a doorway on a main route (no blinding transition). Both values are provisional; the stock distribution on q3dm6, pro-q3dm6, q2dm1–8 and dm1–dm6 is recorded first | BENCH-v1 claimed MP cases, HEROES | compiler output | lightmap probe by region |
| AO, phong and bounce where supported | Cases of a game whose support table marks `_dirt`, `_phong` or bounce as supported, with the keys written and acknowledged in the compile log | 100% of those cases; light time stays within the budget | BENCH-v1 per game | compiler output | run.py log parse |
| Warnings accounted for | Lighting errors; unjustified warnings | 0 errors; every remaining warning justified in the study | HEROES plus BENCH-v1 | static analysis | report findings review |
| Light budget holds | Final-light wall time with fixtures, sun and bounce, at 8 × 2 | Within the budget frozen in Phase 2 and re-measured once in Phase 4 (8.3), on 100% of cases | BENCH-v1 largest cases | compiler output | run.py timers |
| Rendered lighting | Windowed screenshots of the predeclared landmark and key-item views | Pass/fail: 100% of the predeclared views captured, with hashes. User ratings are recorded or null, and reported only | HEROES | engine rendering (capture); review is reported separately | engine `screenshot` per D19 |

**Depends on.** X1, F (F1 before G1 calibration), rank 2 (lighting intent), C (outdoor volumes for G2), and B1 for stock bands.

**Architectural changes (need approval).**
- The LightSchedule stage, and lighting analysis moved ahead of final selection (D6).
- Fixture roles and fixture brushes in the layout IR (D8).
- Worldspawn lighting keys and Q2 emitter metadata, which are serialization changes.
- An analyzer model change that alters quality scores. It lands as its own recorded change.

**Risks.**
- Stored lightmap bytes are not rendered appearance: no overbright or gamma is applied.
- Surface lights, sun and bounce raise light time.
- Analysis before selection raises generation time.
- The analytical and compiled models will disagree at first. Report the disagreement; do not tune the analyzer to the gate.

**Size.** L.

### 9.8 Rank 6 — Layout and construction: height, mechanisms, routes for every game and mode (C)

**Why it matters to players.** Route and height choices are what make repeat play interesting. Evidence is static analysis plus compiler output.
- 15 of 17 paths use the game-blind legacy ring or chain. At seed 42 with 6 rooms, Q1 DM, Q3 DM and Q3 duel produce identical geometry hashes.
- Room floors are at z = 0 in 138/138 maps, and 0/18 floor-level requests were met.
- No mover, teleporter, liquid or jump pad appears in any of the 138 maps, and 40/40 AAS files hold 0 jump-pad, teleport or elevator reachabilities.
- CTF plans asking for strict symmetry are not mirrored.
- Above 8 rooms, Q2 gets reduced treatment.
- Selection is a seeded tie-break, and Splice's 8-room variety fell from 2 to 1.
- The SP loop edge joins the start room to the exit room, so a level can be finished in about 4.5 s.

**Scope.**

*C1, Phase 4: Q2 compact slice, needed by the hero.*
- A ConstructionPlanner stage. The pipeline calls ConstructionBuilder through the three-argument `MapBuilder::build`.
- Multi-level room floors, with at least 2 links per adjacent floor pair and links that skip a floor.
- Outdoor and sky volumes, including a **cliff-wall** variant for "cut into a canyon wall": rooms set into a tall rock-face brush run along one side of an outdoor sky volume, using the F2 canyon kit.
- **Landmarks (LandmarkSpec, D8).** A new ConstructionRequest type with kind, position intent, scale, accent material and light. A landmark is built geometry, such as a furnace block with an emissive opening, a pillar cluster or a tower, and is more than a material accent. Each one has a geometry-and-material signature that occurs once per map. The default planner places at least 2 landmarks in maps of 6 or more rooms (Romero), at decision points.
- The default planner chooses lifts (func_plat/func_door), teleporters, ramps, drops and liquids from v2 fields through typed contracts, reusing ramp_audit, drop_audit, arrival_audit and the lift/teleport contracts.
- Base Q2 uses misc_teleporter with misc_teleporter_dest (g_misc.c:1827-1861). trigger_teleport exists only under the ctf module.
- Drop becomes a ConstructionRequest type.
- Full treatment above 8 rooms.
- Motifs chosen by room shape, role and flow instead of index parity.
- Varied passages and overlooks, with deliberate cramped and open spaces next to each other (C-c12).
- Optional side areas and secret routes off the main loop (C-c11).

*C5, Phase 4: variety and ranking.*
- The variety gate.
- Offered topology gains designed chokes, flanks and dead ends instead of opening every shared face.
- Ranker features (route redundancy, exposure, item route and control metrics) are added one at a time with ablations, as in Lattice.

*C2, Phase 5: Q3.*
- A Q3 compact generator built for Q3 movement: pm_airaccelerate 1 strafe jumping, rocket and plasma jumps, jump velocity 270, g_speed 320. Strafe and rocket routes are tagged optional and never required for an item, flag or exit. They are measured:
  - **strafe-jump links**, gaps or ledges reachable only by strafe jumping: native movement trial with a scripted strafe-jump command sequence in `q3host pmove`;
  - **rocket and plasma jump links**: static analysis only, from the knockback impulse derived from source (g_combat.c; self-damage halved after knockback, g_combat.c:989-992), because native trials have no weapons;
  - every item reachable only through such a link carries `notbot 1`, which botlib reads into IFL_NOTBOT (be_ai_goal.c:630-631), so bots do not chase it.
- Jump and launch pads as trigger_push plus target_position:
  - Arcs come from g_trigger.c AimAtTarget.
  - The bot's 1.1× horizontal prediction (be_aas_reach.c:279) is recorded.
  - Targets above the reachable apex (the NaN case) and below the trigger centre (unverified) are rejected by construction.
- Curved patches in default output, with clip backing.
- Bot hints:
  - common/clusterportal and donotenter in doorways;
  - botclip through an original shader, since none ships, as simple clip around detail and curves;
  - item_botroam always with a weight;
  - `notbot` on items reachable only through optional trick routes;
  - info_camp range/weight not relied on.

*C3, Phase 5: team and objective modes.*
- A CTF mirrored builder that applies `plan.symmetry`, with distinct base identities (F2 kit and G2 colour), at least 3 routes to each flag and a contested midfield (Q2 CTF, Q3 CTF, Threewave if kept).
- A TDM team-scale layer: route width and spawn spread.
- Bot-arena layout.

*C4, Phase 6: Q1, conditional.*
- Motifs for Q1 movement: maxspeed 320; jump 270 (peak about 45.6 u, derived); 18-unit steps; grounded strafe and bunny-hop; rocket jumps under the chosen progs' teamplay self-damage rules. They are measured the same way as C2: strafe and bunny-hop links by native trial, rocket-jump links by static analysis only, and none of them required.
- DM4/DM6-style compact verticality with protected recovery routes (C-c6).

*C6a, Phase 5, with E1 (SP sub-lane): progression-graph skeleton.*
- Start, then keys, then exit, with key-door placement through ConstructionBuilder.
- No loop edge from start to exit.

*C6b, Phase 7, with E2: progression shape.*
- Loops back through cleared areas, and views of later areas.
- Arena and boss spaces for lock-in and boss encounters.

**Generator contracts, each with its counterexample.**
- **C-c1 (Jaquays). Loops and several entrances: at least 2 per major MP room, and a 2-edge-connected MP graph.** Counterexample: a deliberate exposed dead end with a reward.
- **C-c2 (Jaquays). Links between floors in more than one place.** Counterexample: a single lift to a power item as a deliberate commitment or trap (DM2-style), declared with a reason.
- **C-c3 (Jaquays). Links that skip a floor in maps with 3 or more floors.** Counterexample: in compact duel maps they can make height control trivial.
- **C-c4 (Romero). If the player can see a place, they can get there.** "Visible" means a walkable floor region of at least 64 × 64 u: faces with normal z ≥ 0.7 inside the playable hull, excluding sky, hazard, clip and nodraw. The region must be in line of sight from any eye-height navigation sample. "Can get there" means the region is bound to a navigation node reachable from a spawn or start under the game's movement profile, without optional trick links. Counterexamples: vistas, sky and decor outside the playable hull, tagged in the IR as vista volumes; and SP glimpses that become reachable later in the progression.
- **C-c5 (Jaquays). Every room has a purpose.** A room has a purpose when its typed role's content predicate holds: an item or resource room holds at least 1 placement; a spawn room holds a spawn; an arena or encounter room holds an encounter; a landmark room holds a landmark; an objective room holds a key, exit, flag or objective; a connector lies on at least 1 shortest path between two other purposeful rooms. Counterexample: pacing connectors, allowed up to 25% of rooms (provisional).
- **C-c6 (DM4/DM6). Compact verticality with protected recovery routes.** Counterexamples: q2dm1 clustering, and q3dm17's void exposure, which is the design. Protection must not become an unassailable perch.
- **C-c7 (q3dm17). Predictable pad arcs over a void are a sniping risk.** Exposure of an arc is the share of its sampled flight points (every 0.1 s) in line of sight from at least 1 eye-height navigation sample outside the launch and landing rooms. Every Q3 player spawns with a hitscan machinegun, so any sample counts. Each landing gets cover or an alternative route. Counterexample: the risk is the appeal, so exposure is bounded, not zero.
- **C-c8 (q3ctf4). Mirrored bases with distinct identities, 3 or more routes per flag, a contested midfield.** Distinct identity: at least 2 visible material roles differ between the two bases, or their light colour differs by at least 0.3 in R/B ratio, with the structure still mirrored. Contested midfield: a midfield region that at least 2 of the 3 routes to each flag cross or overlook, with traffic at or above the 67th percentile. This is separate from fairness, which is equal travel time. Counterexamples: asymmetric CTF can be fair through compensations, and 3 routes in a tiny map becomes a maze. Measure q3ctf1–5 and q2ctf1–5 first.
- **C-c9 (Romero). The SP level flows back on itself.** Counterexample: linear finales and escape levels.
- **C-c10 (Q3 bots). Clusterportals in doorways and clip around detail and curves; `notbot` where an item has no normal route.** Counterexample: over-clipping removes player trick routes, and a misplaced clusterportal breaks AAS clustering, so BSPC output is gated.
- **C-c11 (Jaquays). Side areas and secret routes.** MP maps of 8 or more rooms have at least 1 optional side area: a dead end holding a reward, or an alternative route off the main loop. SP maps have at least 1 secret route (with E2). Counterexample: side areas in a duel map that hide a player too well. They are bounded by D-c6.
- **C-c12 (Romero). Contrast cramped and open spaces.** At least 1 adjacent pair of spaces per map whose ceiling-height ratio or floor-area ratio is 2 or more (provisional). Counterexample: constant alternation reads as noise. The stock rate is measured first.
- **C-c13 (Romero). Recognisable landmarks in several spots.** At least 2 landmarks with distinct signatures in maps of 6 or more rooms, each visible from at least 2 rooms. Counterexample: in a small duel map, 2 landmarks may crowd the space, so the count follows the size band.

**Deliverables.**
- The ConstructionPlanner stage. A DropSpec type and a LandmarkSpec type. Multi-level generation with a floor-level analyzer. The cliff-wall outdoor variant.
- A Q3 push-pad spec plus PlacementRole push, patch emission, and a q3map2 + BSPC fixture.
- Q3 and Q1 compact generators, each motif with an access-removal unit test.
- The CTF mirror builder plus a route-count analyzer. The TDM layer.
- The SP progression IR: C6a with E1, C6b with E2.
- The variety gate and a ranker feature revision, with weights still hand-set (D30).
- Compiler fixtures per mechanism × game.

**Acceptance tests.**

| Test | Metric | Threshold | Fixed set | Evidence level | Tool |
|---|---|---|---|---|---|
| Height is real | Distinct room-source floor heights against the request; non-flat MP cases with 2 or more room-floor heights; links per adjacent floor pair; floor-skipping links (C-c3) | 100% of floor-level requests met (today 0/18); ≥ 50% of non-flat claimed MP cases (today 0/138); ≥ 2 links per pair, with declared exceptions inside the 5% waiver cap; ≥ 1 floor-skipping link in 100% of maps with 3 or more floors, except compact duel maps that declare the C-c3 counterexample | BENCH-v1, MP-PORTFOLIO, HEROES | static analysis plus compiler output | report `nav_room_floor_heights` plus run.py |
| Mechanisms honoured | Lift, teleporter, liquid, ramp, drop, door and pad requests built with passing recipe audits, or typed unmet where the game contract lacks the mechanism | 100% accounted for; ≥ 90% honoured for Q2 by the Phase-4 exit (today no movers, teleporters or liquids in 138 maps) | BENCH-v1 mechanism cases | static analysis plus compiler output | trace plus ramp_audit, drop_audit, arrival_audit and the lift/teleport contracts |
| Default mechanism choice (P4) | Vague, unrequested maps with 2 or more floors that contain a default-chosen mechanism; audit pass rate of default-chosen mechanisms; evidence per mechanism kind on MECH-SAMPLE | ≥ 50% (provisional); 100%; for every kind × game, native traversal (where a harness exists) and engine initialization on every MECH-SAMPLE instance, the same evidence the recipes carried | BENCH-v1 vague cases, MP-PORTFOLIO, MECH-SAMPLE | static analysis; native movement trial; engine initialization | trace, recipe audits, native tools, H smoke |
| Mechanism compiler fixtures | Each mechanism × game through compile, full VIS and final light (plus AAS for Q3) | 100% pass | `tests/fixtures/mechanisms` | compiler output | `ctest -R construction_compile` |
| Native traversal of mechanisms and motifs | Native completion of each new type, on predeclared representatives | 100% of predeclared traversals complete, and at least 1 kept completion per mechanism per game where a harness exists. A failure fails the row until the generator cause is fixed. A failure blamed on the harness counts until a harness fix passes the stock control routes. Every failure stays in the record | One representative per type from BENCH-v1, MECH-SAMPLE, plus HEROES | native movement trial | `tools/native_movement`, `tools/native_teleport` (Q2); H4 (Q1/Q3) |
| Movement-specific links (Q1/Q3) | Strafe-jump and bunny-hop links completed by native trial; rocket-jump links checked statically; items, flags or exits that need any optional link | 100% of strafe and bunny-hop links complete natively with their scripted sequence; rocket-jump links within the derived knockback limit; 0 required items, flags or exits behind an optional link; 100% of items reachable only through one carry `notbot` (Q3) | BENCH-v1 Q1/Q3 MP cases, Q1/Q3 heroes | native movement trial (strafe); static analysis (rocket) | `q3host pmove`; `native_movement_q1`; trace |
| Q3 jump pads | AAS TRAVEL_JUMPPAD reachabilities per pad; native landing error; arc clearance; arc exposure (C-c7) | ≥ 1 per pad on 100% of pads (today 0 in 40/40); landing within 32 u of target_position; the arc clears the player hull; 100% of landings have cover or an alternative route, and exposure is at or below the highest q3dm17 pad exposure, measured first | BENCH-v1 Q3 pad cases plus Q3 heroes; STOCK q3dm17 | compiler output (BSPC) plus AAS routing plus native movement trial; static analysis (exposure) | `mbspc -aasinfo`, `q3host route`, `q3host pmove`, visibility analyzer |
| Landmarks (C-c13, hero) | Landmark requests built; landmarks per map; signature uniqueness; rooms with LOS to each landmark; position-intent verdict | 100% of landmark requests honoured or typed unmet; ≥ 2 per map of 6 or more rooms; 100% unique signatures; each visible from ≥ 2 rooms; position intent (central, at a decision point) passes its qualifier verifier | BENCH-v1 landmark cases, vague cases of 6 or more rooms, HEROES | static analysis plus compiler output | trace plus visibility analyzer |
| Q3 curved patches | patch_count in curve requests; compile, AAS and G15 | > 0 in 100% of curve requests (today 0/40); valid compile and AAS; G15 passes | BENCH-v1 curve cases plus a new fixture | compiler output | `ctest -R q3_patch` plus run.py |
| Games are distinct | Sorted plane-point hash at a fixed seed and room count | Q1 DM, Q1 duel, Q3 DM and Q3 duel all differ (today identical) | Seed 42, 6 rooms | static analysis | `ctest -R cross_game_geometry` |
| Motif contracts | Access-removal tests per motif | 100% fail closed when an access is removed (P2) | `tests/test_q3_motifs`, `test_q1_motifs` | static analysis | ctest |
| Variety gate | Distinct selected graphs per room count against min(distinct offered, seed count) | Selected ≥ that value, or a typed reason recorded, for 100% of room counts | MP-PORTFOLIO, Lattice fifteen, Splice nine, plus BENCH-v1 per path | static analysis | `tools/mp_portfolio/topology_compare.py`, promoted to CTest |
| Tie-break share and ablation | Selections decided only by the seeded tie-break; ablation record per new ranker feature | ≤ 20%; 100% of features have an ablation | Fixed candidate pools from the same portfolios | static analysis | `trace_candidates`, `ranking_compare.py` |
| Topology contracts | C-c1, C-c2, C-c3, C-c4, C-c5, C-c6, C-c11 and C-c12 pass rates, under the definitions above. For C-c6 on compact vertical maps (2 or more floors, at most 8 rooms): every spawn has a recovery route to armor or health that is out of sight of the top-floor power position for ≥ 50% of its length | 100% of generated MP maps, with waivers within the 5% cap; stock controls classified and recorded first (B) | BENCH-v1 plus STOCK | static analysis | navigation and visibility analyzers |
| CTF structure | Structural brushes matching under the mirror transform (identity materials excluded); node-disjoint routes per flag; midfield travel time from each base; base identity; contested midfield (C-c8) | Plane-set match within 1 u; ≥ 3 routes, or the stock-calibrated minimum; travel time equal within 10% (fairness); identity: ≥ 2 visible roles differ, or light R/B differs by ≥ 0.3; contested: a midfield region crossed or overlooked by ≥ 2 of the 3 routes to each flag, with traffic ≥ the 67th percentile; Q3 confirmed by AAS routing (H2) | BENCH-v1 CTF cases, CTF heroes, STOCK q3ctf1–5 and q2ctf1–5 | static analysis plus compiler output (AAS) plus AAS routing | CTF analyzer plus `q3host` |
| Q3 bot hints (C-c10) | Doorway connections (portal width ≤ 192 u) carrying a clusterportal; BSPC cluster and portal errors; item_botroam without weight; clip coverage of detail and patch edges | ≥ 95% of doorways; 0 errors; 0; 100% of patches and detail brushes taller than the 18-unit step bounded by clip or botclip; G15 still passes | BENCH-v1 Q3 cases, Q3 heroes | compiler output (BSPC) plus static analysis | BSPC log parse; `.map` scan |
| SP progression shape (C6b) | Return routes through cleared areas (C-c9/E-c7); views of later areas from the required path; arena spaces for lock-in and boss encounters | ≥ 1 return route through a cleared area per map of 8 or more rooms; ≥ 1 sightline from the required path into a not-yet-reachable later area per map of 6 or more rooms; 100% of lock-in and boss encounters in an arena room that passes the encounter checks | BENCH-v1 SP cases plus SP heroes (only on "go") | static analysis | navigation with key-state expansion; visibility analyzer |
| TDM scale and > 8 rooms | Spawn count; LOS between team spawn clusters; main-route width; full treatment for 9–16 rooms | ≥ 1.5 × player_max spawns; 0 LOS; ≥ 64 u width (provisional; cover share measured too); 100% of 10–16-room cases get full treatment and pass G1–G15 | BENCH-v1 TDM and 10–16-room cases | static analysis plus compiler output | visibility, navigation, run.py |

**Depends on.** Rank 2 (v2 mechanism fields), X2, rank 1, F1 (liquid materials), D (CTF items for C3), H for the Q1/Q3 native rows, and B for calibration.

**Architectural changes (need approval).**
- The ConstructionPlanner in the default pipeline (D6).
- DropSpec, LandmarkSpec, jump-pad spec and PlacementRole push (D8).
- New Q1 and Q3 generators.
- A mirrored-base mode.
- The SP progression IR.
- A ranker feature-vector change: `feature_count` goes from 8 to N, changing selection for every seed (D23).
- An original botclip shader (D9).

**Risks.**
- This is the largest workstream. Geometry changes invalidate every baseline, so one cause at a time with an ablation. Ship the Q2 slice before the hero, then Q3, then Q1.
- Point lights inside liquid bounds are rejected (a known core limit).
- Lift timing in a running engine is unproven without a connected client.
- Strict mirroring conflicts with variety. The variety gate records the reason.
- The latent stairless 64-unit repair platform must be fixed first.

**Size.** XL.

### 9.9 Rank 7 — Single-player and co-op: encounters, skills, resources, keys, secrets, exits, Q2 units (E)

**Why it matters to players.** Q1 and Q2 SP levels are fixed-slot monster chains. Evidence is static analysis plus compiler output; no SP map has ever been engine-loaded.
- Spawnflags are empty in 30/30 SP maps, so every skill level plays the same.
- There is no ammo or armor. An 8-room Q1 probe has 2785 monster HP against 25 starting shells plus pickup grants.
- Weapons arrive by room index, so the Q1 RL comes before the nailgun.
- Requests: keys 0/5, secrets 0/6, coop starts 0/11, named monsters outside the fixed roster 0/8.
- The exit is an invisible trigger hardcoded to `start` or `base1`, and the loop edge bypasses most encounters.
- 116 unreachable_monster findings ship.
- Co-op prompts produce SP maps where extra players telefrag. R1-s adds coop starts to SP maps in Phase 2.
- Neither Q1 nor Q2 3.19 has a coop-only spawnflag. Q1 skill flags apply to SP and coop alike, since both run with deathmatch == 0 (WinQuake pr_edict.c:931-948), and Q2 NOT_COOP is inert (g_spawn.c:587). One map file therefore cannot carry extra resources for co-op only. Items also do not respawn in coop in either game: the 1996 Q1 progs respawn only at deathmatch == 1, and Q2 respawns only in deathmatch. Weapons stay in coop in both games. The co-op output model is decision D28.

**Scope.**

*E1, go/no-go slice, Phase 5 in parallel on the SP sub-lane (8.3), on the Q2 runtime.*
- A progression graph: start, then keys, then exit, with the start-to-exit loop edge removed (C6a, which lands in Phase 5 with E1).
- Key doors through ConstructionBuilder:
  - Q1: func_door spawnflags 8 (gold, IT_KEY2) and 16 (silver, IT_KEY1) with item_key2/item_key1.
  - Q2: key_* items with a trigger_key whose `item` key relays to a func_door. There is no trigger_unlock.
- Skill spawnflags and a distinct per-skill plan:
  - Q1: 256/512/1024, evaluated only when deathmatch == 0; Nightmare uses the hard set (WinQuake pr_edict.c:931-948), so spawnflags cannot make Nightmare differ; QW ignores skill flags; 2048 in DM.
  - Q2: 256/512/1024/2048. NOT_COOP 4096 is inert in 3.19 and not relied on.
- SP ammo, armor and health tiers from D.
- A static pistol-start simulator per skill.
- Co-op under the D28 model. Recommended (a): a co-op prompt produces its own **co-op build**, with resources scaled for player_max baked into that map, coop starts, and no claim that it plays as balanced SP. An SP prompt that "also works in co-op" gets a shared map with no scaling, and a typed `coop_no_scaling` finding says so.
- A configurable exit destination: `--exit-map` and a v2 field. A missing exit destination is a typed finding, never the hardcoded `start` or `base1`.

*Go/no-go checkpoints.* Q2 SP and Q2 co-op are checked at the Phase-5 exit. Q1 SP and Q1 co-op are checked at the Phase-6 exit, and only if Phase 6 runs, because the Q1 WAD (D9) and Q1 smoke (H3) arrive there. A path that misses its checkpoint is dropped, and the drop is written into README and ROADMAP.

*E2, only on "go".*
- Typed encounters, each with its mechanism from source:
  - ambush: spawnflag 1 in both games. Q1 reads it as `spawnflags & 3` in FindTarget (re-release ai.qc:299-305; 1996 statements 984-988). Q2 reads `spawnflags & 1` (g_ai.c:437, 450; g_monster.c:473);
  - Q2 trigger_spawn: spawnflag 2 (g_monster.c:673-731);
  - Q1 has no trigger-spawn flag. A teleport-in wave uses a targetnamed trigger_teleport that acts only after it is fired, with PLAYER_ONLY (1) clear (re-release triggers.qc:397-418; 1996 teleport_touch statements 10251-10270). This was traced statically and is not engine-tested;
  - lock-in arena, ledge snipers, mixed infighting groups, teleport-in waves.
- Theme-matched rosters covering the stock classnames, each with its real hull. Rosters are data per F kit, in `data/rosters/<game>/<kit>.json`.
- Encounter checks with thresholds (provisional, calibrated on e1m1–e1m8 and base1–3):
  - view of approach: each monster has line of sight from its eye to at least 1 navigation sample on every required approach into its room;
  - cover: at least 1 cover sample, with no line of sight to any monster of the encounter, within 256 u of path from the player's entry;
  - room to retreat: the player's entry offers 2 or more exits, or at least 384 u of retreat path that is out of the encounter's line of sight;
  - infighting: a mixed-infighting encounter holds at least 2 monster classes with line of sight to each other, under each game's infighting rule, which is verified from source before encoding.
- At least 2 fairly hinted secrets (Q1 trigger_secret, Q2 target_secret), with kill and secret counts. A **hint** is a cue linked to the secret in the IR and present in the compiled map: (a) an accent or trim material on the secret door or trigger face that differs from its wall run; (b) a light at least 1.2 × the local median at the entrance; (c) line of sight from the required path to the secret area or its reward; (d) a message or sound key on the trigger.
- Kill counts that match each game's counting rule. The 1996 Q1 progs count each fish twice (statements 8606, 8682), so the generator either avoids fish under the 1996 contract or reports the engine's count.
- Objectives: Q2 target_help (spawnflag 1 as primary is a mapping convention, not code) and target_goal.
- Visible exits with a prerequisite.
- A Q2 unit generator: `*` unit prefixes, `$spawnpoint` arrivals at targetnamed info_player_start, return changelevels, target_crosslevel_trigger/target, and a cross-map validator. An optional **secret level** per unit is entered through a changelevel inside a secret area and returns to the unit.

**Generator contracts, each with its counterexample.**
- **E-c1 (E1M1). Introduce enemies and mechanics gradually.** Counterexamples: shock openers and later episodes.
- **E-c2. Weapons before the monsters that need them.** Counterexamples: a deliberate escape encounter with a clear route out; an early weapon in a secret as a reward.
- **E-c3. Health and armor before hard fights.** Counterexample: reward-after-fight pacing.
- **E-c4 (E1M7). The boss is beaten by using the arena.** Hypothesis only for rc1. Counterexample: gimmicks unreadable without hints, or that break in co-op.
- **E-c5. Nightmare is harder, not a random flood.** The "not a flood" half holds by engine code in Q1: Nightmare reuses the hard spawnflag set (WinQuake pr_edict.c:931-948), so it cannot add monsters. The "harder" half rests on progs behaviour read statically from the 1996 disassembly and never run: the attack cooldown is dropped at skill 3, and the Vore and Shambler have skill-3 boosts. Counterexample: the re-release sets the player to 50 HP on Nightmare, so resource needs depend on D13.
- **E-c6 (Romero). Several fairly hinted secrets per level.** Counterexample: secrets holding required resources.
- **E-c7 (Romero). The level flows back on itself.** Counterexamples: linear escape levels, and empty backtracking.
- **E-c8. Q2 unit/hub backtracking with field-computer objectives.** Counterexample: revisits without a changed state are padding.

**Deliverables.**
- The EncounterProgression stage.
- Skill-flag emission with per-skill plans.
- `tools/sp_budget` (the pistol-start simulator).
- Key, lock, secret and objective lowering.
- The Q2 multi-map unit writer and unit validator.
- Co-op contracts.
- knowledge/sp linkage (with B2).

**Acceptance tests.**

| Test | Metric | Threshold | Fixed set | Evidence level | Tool |
|---|---|---|---|---|---|
| Key gating | Key reachable before its lock; exit unreachable without each key; loop bypasses | 100%; 100%; 0 | BENCH-v1 Q1/Q2 SP and co-op cases plus new key cases | static analysis | navigation with key-state expansion |
| Skill flags | Flag coverage on monsters and items; per-skill counts; Q1 Nightmare set | 100% flagged (today 0/30); easy ≤ normal ≤ hard; Q1 Nightmare equals the hard set; the stock e1m1/base1 band is recorded | BENCH-v1 SP cases plus STOCK-Q1/Q2 SP | static analysis | validator plus B1 entity report |
| Pistol-start completability | Per skill along the required route: available damage (ammo × weapon damage from id source) against each encounter's monster HP; health/armor before encounters | Damage ≥ 1.25 × HP for every encounter at every skill; health and armor above the stock-calibrated floor; 100% of cases. Margins are calibrated on e1m1–e1m8 and base1–3 and recorded first | BENCH-v1 SP and co-op cases, STOCK SP calibration | static analysis (simulation, no monster AI) | `tools/sp_budget` |
| Weapons before monsters | Encounters whose required weapon tier exceeds what the required path provides; ammo for unobtainable weapons | 0; 0 | BENCH-v1 SP | static analysis | simulator |
| Co-op | Coop start count and overlap; under D28 (a), resource totals of a co-op build on the required route, compared with the SP build of the same brief and seed; shared maps carrying the `coop_no_scaling` finding | ≥ player_max − 1 starts; 0 overlaps; co-op build ammo and health ≥ the SP totals × the co-op factor predeclared in the profile (provisional, recorded before measurement) in 100% of co-op builds; 100% of shared maps flagged | BENCH-v1 co-op cases | static analysis | validator plus `tools/sp_budget` |
| SP analysis errors | Error-severity findings shipped (unreachable_monster, monster_cannot_reach_player, embedded monster, pickup deleted at spawn) | 0 (today 116 unreachable_monster; 29 navigation errors in the 8-room Q1 SP case) | BENCH-v1 SP and co-op | static analysis | G11 |
| Engine SP and co-op | Initialization under coop 1 at skill 0–3. A Q2 dedicated server forces deathmatch 1 unless coop is set (sv_init.c:318-324), and monsters are freed in deathmatch (g_monster.c:534-538), so SP maps are initialized under coop 1 as an SP proxy (D31). SP-only behaviour (single-start selection, help computer, changelevel) stays static unless D20 approves a client | 100% of Q2 SP and co-op cases under the coop-1 proxy. Q1 only if H3 exists, and only after its server is shown not to force deathmatch (H3) | BENCH-v1 Q2 SP/co-op | engine initialization (coop-1 proxy for SP) | H1 coop profile |
| Go/no-go | The tests above on q2-sp and q2-coop at the Phase-5 exit; on q1-sp and q1-coop at the Phase-6 exit (only if Phase 6 runs) | 8/8 core cases each for SP and co-op per game at its checkpoint, otherwise those paths are dropped and the drop stated | BENCH-v1 SP/co-op | static analysis plus engine initialization (coop-1 proxy) | ledger decision record |
| Gradual introduction (E-c1) | Monster tier order along the required path; first appearance of each mechanic | The first encounter uses only the roster's lowest tier; the highest tier met does not decrease over the first third of encounters; each new mechanic (lift, key door, hazard) first appears in a room with no encounter above tier 1. Provisional, recorded on e1m1 and base1 first | BENCH-v1 SP plus SP heroes | static analysis | encounter analyzer |
| Encounter variety (E2) | Distinct typed encounters; encounter checks as defined in E2; named monsters | ≥ 5 kinds across the SP set and ≥ 3 per map of 8 or more rooms; 100% of encounters pass view of approach and room to retreat; ≥ 1 cover sample per encounter; ≥ 1 infighting pair per infighting encounter; named monsters honoured or typed unmet. Q1 kinds traced only in the re-release QuakeC and the 1996 disassembly are labelled static | BENCH-v1 SP plus SP heroes | static analysis | encounter analyzer plus trace |
| Theme-matched rosters (E2) | Monsters whose classname is in the selected kit's roster | 100%, or a typed reason (for example a monster the prompt named) | BENCH-v1 SP plus SP heroes | static analysis | roster check against `data/rosters` |
| Secrets (E2) | Secrets per level; hint cues as defined in E2 | At least the id1 per-level median measured by B1 (≥ 2 for levels of 8 or more rooms); 100% of secrets with ≥ 1 cue present in the compiled map; ≥ 50% with a visual cue (a–c) | BENCH-v1 SP | static analysis plus compiler output | validator |
| Kill and secret counts (E2) | Expected total_monsters and total_secrets per skill, against the count the engine's rule would give | Equal in 100% of SP maps; under the 1996 Q1 contract, fish counted twice or avoided | BENCH-v1 SP | static analysis | validator with the profile's counting rule |
| Objectives and exits (E2) | Q2 target_help entities fired on the required path before the objective they announce; target_goal entities reachable and triggerable on the required path; exit destination taken from `--exit-map` or the v2 field | 100%; 100%; 0 hardcoded `start` or `base1` exits | BENCH-v1 Q2 SP plus unit prompts | static analysis | validator |
| Unit travel (E2) | Changelevel and spawnpoint targets resolved across a unit; secret level entered only from a secret area and returning to the unit | 100% across every generated unit (at least 2 units of at least 3 maps); 100% of secret levels. Arrival in the engine only if D20 approves a client | Fixed unit prompts | static analysis; engine initialization of each map under the coop-1 proxy only | unit validator plus H1 |

**Depends on.** Rank 1, rank 2, X2, C (C6a for E1, C6b for E2, doors via ConstructionBuilder), D (resource model), H1, B (SP calibration), and D28 (co-op model) before E1 starts.

**Architectural changes (need approval).** The EncounterProgression stage (D6). The SP progression IR, skill and spawnflag fields, key/secret/objective roles (D8). Multi-map Q2 unit output (D22). The exit-destination CLI option. The co-op output model (D28).

**Risks.**
- This is the largest workstream with the weakest runtime evidence. Static completability is not play, and there is no monster-AI evidence.
- The Q1 contract changes Nightmare and spawn semantics.
- Unit generation multiplies compile time.
- Co-op cannot get ahead of SP.
- No SP map can be engine-initialized as SP on a Q2 dedicated server. The coop-1 proxy differs from SP in start selection and in weapon stay.

**Size.** XL (E1 L, E2 XL).

### 9.10 Rank 8 — Validation and runtime evidence (H)

**Why it matters to players.** These are the gates that catch the failures players hit first: fatal match ends, items in solid, missing spawns, bots that never reach items.
- Q1 and Q3 stop at compiler output.
- No SP map can be engine-initialized as SP on a Q2 dedicated server (deathmatch is forced, sv_init.c:318-324), and Q3 SP needs `spmap` (sv_ccmds.c:179-181).
- Q3 AAS is checked only as global category counts (RunQ3CompilerTest.cmake:205-275). The ctf8 fixture passes with a flag the project's own analyzer reports unreachable. On stock q3dm17 the existing reader binds 13 of 32 anchors to area 0.
- The Q2 smoke covers deathmatch only, is not in CTest, and cannot be rebuilt from its documented root. E:/Games/Q2REPRO hashes differently from the pin. It has passed on 14 studies, but only 6 of those are default-generator BSPs, all predating the current build.
- No connected player, bot or human run exists in any game. RC4 is unmet for Q1 and Q3.

**Scope.**
- **H1, Phase 2.** Make the Q2 smoke reproducible: rebuild Q2REPRO aeeb335f from a documented root (D17). Profiles:
  - deathmatch 1;
  - deathmatch 1 + dmflags 64 (TDM, D14);
  - coop 1 at skill 0–3, used for co-op maps and, as an SP proxy, for SP maps (D31);
  - game ctf, using a source-reviewed x64 build of the 3.19 ctf DLL (D21).

  There is no "deathmatch 0" SP profile. A dedicated server forces deathmatch 1 unless coop is set (3.19 sv_init.c:318-324; q2repro-current src/server/init.c:437-442), and monsters are then freed (g_monster.c:534-538). Such a profile would silently test a DM map. The rebuilt aeeb335f is checked for the same forcing, and a control asserts that monsters exist after initialization under coop 1 (base1).

  Controls: stock q2dm1, base1 and q2ctf1, plus the buried-RL negative. Register optional, hash-gated CTest groups. A skip reports "not configured" and the ledger records it as not run.
- **H2, Phase 3.** `tools/q3host`, a separate GPL process that links unmodified botlib and qcommon cm_* sources from 1.32b through a minimal shim, as tools/native_movement does for Q2.
  - Binding: it binds spawns, items, flags and team spawns to AAS areas the way botlib does (point area, best-reachable-area fallback).
  - Routing: with every travel type, including jump pad (18), teleport and elevator. Rocket jump is reported separately.
  - Calibration: on stock AAS, with 0 false failures.

  Also a Q3 dedicated smoke: a Quake3e build per D18, stock qagame.qvm with its hash recorded, isolated homepath, loopback only. FFA, tourney, TDM and CTF (g_gametype 0, 1, 3, 4) load with `map`. Bot arena loads with `spmap`, because `map` resets g_gametype 2 to FFA (1.32b sv_ccmds.c:179-181; Quake3e sv_ccmds.c:202-204).

  Then server-side bot runs on heroes: a fixed frame budget, with pickups parsed from the server's own console output. On a dedicated server, `G_LogPrintf` echoes every log line to the console (g_main.c:1095-1097), so the same parser works under GT_SINGLE_PLAYER, where g_log is never opened (g_main.c:430). Remote challenges and status queries are ignored in SP (sv_client.c:53, sv_main.c:318), so the driver reads only stdout. Bots come from `addbot` in the MP gametypes, and from the `.arena` "bots" key in bot arena (G_InitBots, g_bot.c:976-1010). Whether SP bots start with no human client connected is unverified. A stock SP arena control (q3dm1 from arenas.txt) must pass first; otherwise D15's fallback applies. The log line format is verified in g_items.c first.
- **H3, Phase 6, conditional.** Q1 dedicated smoke per D16 and D13. Profiles for SP skill 0–3, coop 1, and deathmatch 1 with teamplay 0/1. Controls: dm3 and e1m1 positives; item-in-solid and missing-start negatives. Before the SP profile is trusted, the chosen server is checked for deathmatch forcing. WinQuake sets deathmatch 1 at init when maxclients > 1 (host.c:197-200). A later `deathmatch 0` is not overridden, because SV_SpawnServer forces 0 only when coop is set (sv_main.c:1072-1073). fteqw-sv is unverified. The e1m1 control must show monsters present under the SP profile.
- **H4, Phases 5–6.** Native harnesses:
  - Q3: bg_pmove, bg_slidemove and cm, plus the AimAtTarget/BG_TouchJumpPad pad code, in q3host.
  - Q1: QW pmove.c world-only with the NQ differences documented, or a WinQuake sv_phys/sv_user subset.
- **H5, Phase 3.** Error-severity analysis findings promoted to the G11 gate under the waiver rules (8.3). Navigation and reachability errors are hard from the Phase-3 exit, and `unlit_critical_placement` from each game's lighting exit. Compiler fixtures run with `--report`.
- **H6, Phase 2.** A human playtest protocol, `docs/PLAYTEST_PROTOCOL.md`, derived from MAP_STUDY.md:76-101 and study-plan.template.json.
  - Questions per mode: SP pistol-start completion per skill, secrets found and whether their hints were noticed; co-op start spread; DM/TDM spawn deaths and item control; duel item timing and the losing player's recovery; CTF carrier routes and side-swap fairness; Q3 bot-arena behaviour; landmark recognition ("where are you?" asked at fixed points).
  - Session lengths and player loads.
  - Data fields: map, BSP, engine and module hashes; dmflags; player count; demo; screenshots; anchored 1–5 or null ratings; counterexamples.

  Plus a screenshot harness (`tools/screenshot`): a scripted cfg running map, camera console command, screenshot, quit. Movement commands such as `+forward` count as input and are not used. If an engine has no camera command, captures are limited to spawn and intermission views, and the study says so.

**Deliverables.**
- `docs/ENGINE_BUILD.md`, with the rebuild record and new pins.
- Q1 and Q3 smoke drivers with licence-boundary READMEs.
- `tools/q3host` (bind, route, pmove) with calibration records.
- Q1 and Q3 native hosts.
- CTest groups `engine-smoke-q1/q2/q3` and `q3-reach`.
- The G11 gate list with waivers.
- PLAYTEST_PROTOCOL.md plus a session-record JSON schema.
- The screenshot procedure per engine.

**Acceptance tests.**

| Test | Metric | Threshold | Fixed set | Evidence level | Tool |
|---|---|---|---|---|---|
| Q2 smoke reproducibility | Rebuilt binary hashes against the pins; verdicts on earlier evidence | Hashes match, or a re-pin with a recorded source review; same verdicts on 14/14 delivered studies. A differing verdict fails this test, and the ledger marks that earlier evidence "not reproduced"; it is never re-labelled as passing | out/*/engine-smoke BSPs (weft, splice, lattice, braid, hinge, span plus 8 recipe maps) | engine initialization | `engine_smoke.py` from the rebuilt root |
| Q2 profile controls and coverage | Control classification per profile; Q2 cases passing under their own profile; monsters present after coop-1 initialization of base1 (proves the SP proxy is not a DM load) | 100% of controls; 100% of claimed Q2 BENCH-v1 MP cases and every Q2 hero (today DM only); SP and co-op cases under coop 1 are reported from Phase 3 and gate at the E1 checkpoint | Stock q2dm1, base1, q2ctf1, buried-RL fixture; BENCH-v1 Q2 | engine initialization | `engine_smoke --profile {dm,tdm,coop,ctf}`, optional CTest |
| Q3 binding calibration | Item, weapon, spawn and flag entities bound to a nonzero AAS area | 100% (the old reader left 13/32 at area 0 on q3dm17) | STOCK-Q3 q3dm17, q3ctf4, q3tourney2, q3dm6, q3tourney4 | AAS routing (botlib binding over the shipped AAS) | `q3host bind` |
| Q3 route calibration | Unexplained unroutable spawn-to-item and base-to-flag pairs on stock maps | 0; every exception cites a botlib line | Same calibration set | AAS routing | `q3host route` |
| G15 per-entity reachability | Unroutable spawn-to-item and base-to-enemy-flag pairs on generated maps; items without `notbot` that are reachable only by optional trick links | 0 on every claimed Q3 case and hero; the ctf8 fixture fails until it is fixed; 0 | BENCH-v1 Q3 plus `qmg_q3_ctf8` | compiler output (BSPC) plus AAS routing | `run.py --gates G15` |
| Q3 smoke | Controls and case verdicts per gametype with bots to capacity: `map` with g_gametype 0, 1, 3, 4; `spmap` (GT_SINGLE_PLAYER) for bot arena | Controls 100%, including the stock SP arena control under `spmap`; 100% of claimed Q3 cases; 0 "no spawn point", 0 startsolid drops, 0 telefrags on join. Under `spmap` the server reports gametype 2, and console output is the only channel | q3dm1 (SP control), q3dm17, q3ctf4, item-in-solid fixture, BENCH-v1 Q3 | engine initialization plus engine runtime with bots | `tools/engine_smoke_q3` |
| Q3 hero bot runs | Items picked up by a bot at least once across K = 5 fixed-budget runs with 3 bots. K, the bots, the frame budget and the seeds are fixed in HEROES before the run. Pickups are read from console output, so the method also works under `spmap` | 100% of items. A miss fails the hero's RC4 row; the record is kept, and the hero is rerun on the same seed after a generator fix. This is a hero gate, not a benchmark gate, because bot runs are not deterministic | Q3 HEROES | engine runtime with bots | bot-run harness |
| Q1 smoke | Controls per profile, including monsters present in e1m1 under the SP profile; claimed cases; "fell out of level", "no info_player_start" lines | 100%; 100%; 0. Match-end "Program error" is checked only with a loopback client (D20) | dm3, e1m1, negative fixtures; BENCH-v1 Q1 claimed | engine initialization | `tools/engine_smoke_q1` |
| Native calibration | Q3 pad landing distance; Q3 item routes; Q1 jump peak and stair/jump routes; scripted strafe-jump sequences | Q3 ≤ 32 u on 100% of q3dm17 pads; ≥ 90% of predeclared q3dm6/q3tourney2/4 routes complete, failures kept (stock routes, so a shortfall here is a harness defect to fix before generated maps are judged); Q1 peak 45.6 ± 1 u; dm4/dm6 routes complete; the strafe sequence beats the ground-speed jump distance on a flat control | STOCK control routes, a flat strafe control | native movement trial | `q3host pmove`; `native_movement_q1` |
| G11 analysis-error gate | Error-severity findings on delivered cases without a waiver; waiver share | 0, in the hardening order set in 8.3; waivers ≤ 5% per gate per path, each user-approved | BENCH-v1 | static analysis | `run.py --gates G11`; fixtures with `--report` |
| Playtest protocol | Claimed paths covered by question sets and data fields; schema validity | 100%; valid; README/ROADMAP state whether it was executed | Claimed set | static analysis (a document and schema check). It produces no human-play evidence | `ctest -R playtest_protocol`, user review |
| Screenshot provenance | Hero screenshots made by the engine command, windowed, hashed with engine hash and cfg; sessions with recorded user approval | 100%; 0 OS captures; 100% approved | Every hero and capture session | engine rendering | `tools/screenshot` record check |

**Depends on.** X1, rank 1 (mode and module for the profiles), B1 (AAS reader pieces), and D16–D21.

**Architectural changes (need approval).** New separate-process GPL tools: q3host, the ctf DLL, a Q1 server, Q1 and Q3 pmove hosts (D21). Optional engine-launching CTest groups. Smoke profiles keyed to the report v5 mode and module, with the coop-1 SP proxy (D31) and `spmap` for bot arena (D15). The added evidence-level names (D34).

**Risks.**
- Quake3e's Windows dedicated server creates a console and calls SetForegroundWindow (win_syscon.c:871). Q2REPRO and ironwail clients also take focus.
- PE builds are rarely bit-reproducible, so a re-pin is likely.
- Dedicated initialization cannot test intermission, $spawnpoint arrival or pickups without a connected player.
- Dedicated servers can silently change the mode: Q2 forces deathmatch 1 without coop, and Q3 `map` resets SP to FFA. Each profile therefore carries a control that proves the mode it claims to test, such as monsters present or gametype 2 reported.
- Re-release QuakeC needs engine localization support.
- The local qagame.qvm version is unverified.

**Size.** XL.

### 9.11 Rank 9 — Classic-map study and knowledge (B)

**Why it matters to players.** B does not change the maps players load. It turns rules into measured thresholds; without it, the D, F, G, C and E thresholds are guesses.
- Q1 study scores 0: tools/map_study rejects anything but IBSP38 (binary.py:178-181).
- Q3 scores 1: there is only a six-map QL atlas reader, and its AAS graph drops jump-pad travel.
- No stock map is calibrated in any game. q2dm1–8, q2ctf1–5 and base1 are absent, so the hero's q2dm1–q2dm8 comparison and the q2dm1 Edge counterexample cannot be measured today.
- The 60 cards cover Q2 MP only, and nothing in src/ or the benchmark reads them.

**Scope.**

*B1, Phase 2–3, starting early.*
- BSP29, BSP2 and IBSP46 study readers that reuse the bounded Lumps and entity parser and keep the local-only, no-geometry-copy rules.
- A synthetic BSP2 fixture compiled with ericw `qbsp -bsp2`, since no BSP2 sample exists on disk.
- Corrupt-envelope fixtures.
- A stock provenance path: id Software plus the per-map designer where documented; licence "commercial EULA, local reference only"; `training_eligible=false`; stock maps in their own groups instead of study_only.
- Mode-aware entity semantics from the X2 profiles: skill flags, NOT_DEATHMATCH, Q2 NOT_COOP recorded as inert, monsters, keys, secrets, changelevel, coop starts; Q3 gametype/notfree/notteam/notsingle.
- Cohorts per claimed game × mode with at least 5 independent groups, or marked insufficient.
- **Design-history claims measured before use:** item clustering in q2dm1 (the Edge counterexample), item sides in dm3 (the power and recovery sides), and the camper-trap mechanism in dm2, all from stock entity lumps. Each result is recorded as confirmed or unconfirmed before any D contract uses that map as a control.
- Q3 stock AAS routing shared with q3host.
- ztn3tourney1 and hub3aeroq3 are not on disk (see the fact corrections at the top). They are studied only if the user supplies them with origin, author, licence and hash (D24). Otherwise the Q3 tourney study uses q3tourney1–6 and pro-q3tourney2/4, and the study says the two community maps were not studied.
- `validate_profile` accepts q1, q3 and non-DM entity policies, and the benchmark subcommand stops hard-coding `--game q2`.

*B2, Phases 4–7.*
- Knowledge card schema v2: game/mode scope, enumerated status and evidence levels. v1 stays readable and no card is deleted. A JSON Schema plus a CTest validator.
- knowledge/q1, q3 and sp card sets, plus Q2 extensions for CTF, TDM, co-op and SP units. Every new card has the six fields the brief names (principle, mechanism, generator_action, measure, counterexample, experiment) and status `design_hypothesis`, and keeps its counterexample.
- `knowledge/brief_rules.json` lists every classic rule in the brief (Romero, Jaquays, Quake, Quake II, Quake III, respawn facts), each with an id and the card ids that encode it. A rule can be marked "not encoded in rc1" only with a stated reason.
- A card's `measure` field holds only a scalar aggregate, with its source map ids and script hash (D29).
- Seeds:
  - the Romero and Jaquays rules (F-c, G-c, C-c contracts);
  - DM3 power/recovery (sides measured in stock dm3 entities first); DM2 camper trap (hypothesis only, mechanism verified in stock dm2 entities first, encoded as D-c10); DM4/DM6 compact verticality; FFA Quad far from RLs; no-camp-overlook;
  - E1M1, E1M7, weapons before monsters, Nightmare-not-a-flood;
  - the q2dm1 Edge counterexample card; Q2 unit/hub backtracking; target_help as a convention; q2-not-coop-inert;
  - Q3 RA/MH/YA control with the q3dm6 and q3tourney4 pro- comparisons; q3dm17 pads; q3ctf4 bases; Q3 bot hints;
  - respawn and gametype-token fact cards with the discrepancies kept.
- A consumer: run.py compares each compiled case with its cohort bands, and reports cite applied card ids.
- Re-run the map_study benchmark on the current generator with full VIS and light.

**Deliverables.**
- Readers and tests.
- The stock provenance policy in corpus.py.
- Cohort profiles per game × mode: tracked as aggregates only if D29 permits, otherwise under `out/` with the hash tracked.
- Stock measurement tables, including q2dm1–q2dm8 for the hero, under the same D29 rule.
- Per-game study docs.
- Card schema v2 and validator.
- Seeded cards.
- MAP_STUDY.md, DATASET.md and ROADMAP M4 updated with counts and evidence levels.

**Acceptance tests.**

| Test | Metric | Threshold | Fixed set | Evidence level | Tool |
|---|---|---|---|---|---|
| Readers on stock | Stock BSPs parsed under bounded validation; corrupt envelopes rejected | 51/51 id1, 70/70 Q1RM, 36/36 baseq3 including the pro- maps, q2dm1–8, q2ctf1–5, base1 and the other local units, plus the BSP2 fixture; 0 quarantined; 100% of corrupt fixtures rejected | STOCK-Q1/Q2/Q3 plus corrupt fixtures | static analysis | `ctest -R qmg_map_study_tests` |
| Entity semantics cross-check | Per-skill and spawn-type counts against the Phase-1 audit | Exact: e1m1 has 32 not_easy monsters and 7 ambush; base1 has 16 of 19 monsters ambush and 2 triggered spawns | e1m1, base1 | static analysis | map_study entity report |
| Stock provenance | Stock records with attribution, licence status, `training_eligible=false` and their own groups | 100%; 0 stock maps in study_only | All stock records | static analysis | corpus validation |
| Cohort sufficiency | Independent groups per claimed game × mode | ≥ 5, or marked insufficient with the shortfall recorded, for 100% of claimed paths | All corpora | static analysis | `map_study profile --strict` |
| Hero comparison | Descriptor bands for q2dm1–q2dm8 | 8/8 measured and reported in the hero study | STOCK-Q2 q2dm1–8 | static analysis | `map_study benchmark --game q2 --stock` |
| Card integrity | Schema and evidence violations; new cards missing any of the six named fields or not at status `design_hypothesis`; card id set against the previous set | 0; 0, and every card has a counterexample; 0 deletions (the 60 Q2 cards kept) | `knowledge/**` | static analysis | `ctest -R knowledge_schema` |
| Brief-rule coverage | Classic rules in `brief_rules.json` without a card, or without a stated reason for not being encoded in rc1 | 0 | `knowledge/brief_rules.json` against the brief | static analysis | `ctest -R knowledge_rule_coverage` |
| Design-history controls | q2dm1 clustering, dm3 sides and dm2 trap measured and recorded before use | 3/3 recorded before any D contract measures a generated map; unconfirmed claims are not used as controls | STOCK-Q1 dm2, dm3; STOCK-Q2 q2dm1 | static analysis | map_study entity report |
| Measured before promotion | Status changes without a stock measurement record (counterexample maps included) | 0 | `knowledge/**` | static analysis | `ctest -R knowledge_promotion` |
| Benchmark consumer | Compiled cases with a cohort comparison; reports citing card ids | 100% of cases with a usable cohort; 100% | BENCH-v1 | static analysis | `run.py --reference-compare` |
| Locality | Reference geometry, entity lumps, lightmap or image bytes, or per-map raw outputs under tracked paths; tracked stock-derived aggregates outside the D29-approved locations or without source ids and script hash | 0; 0 | `git ls-files` | static analysis | CTest locality check with the D29 aggregate schema |

**Depends on.** X1, and X2 for entity semantics.

**Architectural changes (need approval).** Knowledge card schema v2. A stock-provenance class and the reference-map policy (D24). Tracked knowledge/ and tools/ trees (D1). The permitted-aggregate rule (D29) and calibration-not-training rule (D30).

**Risks.**
- Stock cohorts are small (q3tourney1–6), so "insufficient" is likely for some modes.
- The community maps the brief names for Q3 (ztn3tourney1, hub3aeroq3) are not on disk, and their licences are unreviewed.
- The 1996 Q1 facts come from disassembly.
- Quake Live remakes are not Q3 references.
- Human lineage review of the splits is still pending.

**Size.** L.

### 9.12 Rank 10 — X3 Heroes, packages and plain-gap documentation (RC3, RC5; touches B and H)

**Why it matters to players.** Each hero is the one map per path that a player or reviewer actually opens, and the docs tell users what they are getting.
- README.md and docs/ROADMAP.md never mention co-op, TDM, Threewave, bot arena or tourney (0 grep hits).
- They never say that no human playtest has been done.
- Their counts are stale: ROADMAP:55 says "15 rule hypotheses" against 60 cards, and README gives both 48 and 60.
- No hero exists for any path. The packages in out/ are Q2 DM iteration studies, not heroes chosen in advance.
- The portfolio exporter refuses anything but Q2 DM/duel (tools/mp_portfolio/export.cpp:427-429).

**Scope.**

*Phase 2: docs truth patch.*
- Fix the counts.
- List the claimed and unsupported modes and the vocabulary gaps.
- State "No human playtest of any generated map has been done."
- State the evidence level per path.

*Generic hero tooling (`tools/hero`).*
- `declare` writes the HEROES entry and ledger entry before any generation. If the hero prompt is already a BENCH-v1 case, `declare` also adds it to `sealed.json`.
- Then: generate through `qmg generate` (never a recipe), compile, analyze, smoke, native and bot routes, screenshots, package, study-doc skeleton.
- The exporter and viewer support every game and mode: per-floor plans; item, spawn, flag and team overlays; SP key/lock overlays.

*Per path.*
- Deliver `out/<name>/`: map, BSP with full VIS and final light, AAS for Q3, report v5, viewer index.html, package zip with hashes and no stock assets or binaries.
- Write `docs/<Q1|Q2|Q3>_<NAME>_STUDY.md` in the existing style.
- Keep failures. If a hero fails a gate, fix the generator cause and rerun the **same** seed; never re-roll.

*Phase 8: RC text.* The claimed set, dropped and deferred paths with reasons, the evidence level reached per path, what each gate does not prove, required modules and contracts, the Q1 WAD and package rule, the scope of determinism, and human-playtest status. All of it is checked against the ledger.

**Deliverables.**
- The README/ROADMAP truth patch.
- `tools/hero`.
- HEROES entries for every claimed path.
- A hero package and study doc per claimed path (section 10 lists them).
- The RC statement.

**Acceptance tests.**

| Test | Metric | Threshold | Fixed set | Evidence level | Tool |
|---|---|---|---|---|---|
| Plain-gap docs | Required statements: human-playtest status, claimed and dropped paths, evidence level per path, gaps per area A–H, unsupported vocabulary; card counts equal to knowledge/ | Present for every claimed and dropped path; counts equal; claimed set identical to the ledger | README.md, docs/ROADMAP.md | static analysis | `ctest -R qmg_docs_claims` plus user review |
| Hero pre-registration | HEROES commit precedes first generation; manifest sha256 in the hero report; seed changes after declaration; generations of the hero prompt at its primary seed before the hero run; generations of sealed cases after baseline-0 | 100% of heroes; 0 seed changes; 0; 0; every attempt kept | HEROES, `sealed.json`, run.py logs | static analysis | git log order, run.py seal log, `qmg_rc_ledger` |
| Exporter generality | Viewer renders a fixture of every claimed game × mode | 100%, including the Q1 BSP29 and Q3 IBSP46 fixtures | One CTest fixture per claimed path | static analysis | `ctest -R hero_export` |
| Package completeness | Required artifacts present with matching hashes; stock assets or engine binaries inside the zip | 100% per claimed path; 0 (checked against a hash list) | HEROES | compiler output | `tools/hero verify` |
| Study completeness | Required sections: brief and per-request trace; layout and floor plans; item control and the losing player's recovery; spawn logic and sightlines; theme, material and light schedule; every gate result; reference-map comparison; smoke, native and bot results; screenshots or their stated absence; what is unproven; human-playtest status. For the Q2 DM hero, also the defaults run and the control-seed run | 100% | HEROES | static analysis | `tools/hero verify` plus doc review |
| Defaults run (Q2 DM) | "a Quake II deathmatch map" alone, through `qmg generate` at the hero's primary seed: hard gates; defaulted-field marking; report of the chosen defaults | Passes every hard gate G1–G14 that is hard for Q2 DM at that point; 100% of defaulted fields marked; the study lists each default chosen (theme, player range, size band, floors, mechanisms, landmarks, item set) and compares it with the hero | The defaults prompt, declared in HEROES with the hero | static analysis plus compiler output plus engine initialization | `tools/hero` |

**Depends on.** X1, rank 1, and per hero the ranks its path needs. The Q2 DM hero needs ranks 1–5, C1 (including LandmarkSpec and the cliff-wall variant), C5, B1 (q2dm1–8), H1 and the screenshot decision.

**Architectural changes (need approval).** None in the core. The exporter is generalized.

**Risks.**
- Tuning a hero after seeing its output. Pre-registration, the seal on q2-showcase-hero and a primary seed that no fixed set contains guard against it. Development still sees the hero **prompt** (through the dry run), but never the hero's output.
- Screenshots are blocked until D19 is decided.
- Pressure to claim weak paths. The brief says to drop them instead.

**Size.** L.

## 10. Sequencing

Three lanes run in parallel:

- **Generator lane.** Changes output. One cause at a time per fixed set, so it is serial within each of its two disjoint sub-lanes: MP and SP (8.3). A change that touches both waits until both are clear.
- **Harness lane.** H, the B readers and calibration, the probes. Changes no generator output, so fully parallel.
- **Docs and evidence lane.** Truth patch, protocol, ledger.

| Phase | Contents | Parallel lanes | Exit gate |
|---|---|---|---|
| **Phase 1 close-out (now): approval** | The audit, draft benchmark, area scores, path readiness and this plan. Nothing architectural changes before approval. | — | The user has answered D1 and the Phase-2/3 approvals (D2, D4, D5, D6 Phase-3 subset, D7, the D8 subset for R1-s and D-a, D10, D11 including (5) and (8), D12), plus D13–D15, D17, D26 (seed and seal), D29, D30, D31, D33 and D34. Answers are recorded in the plan. |
| **Phase 2: measuring stick, then entity safety fixes** | X1 (BENCH-v1 and the other fixed sets, the hero seal, gate fixes, compact Q2 fixtures, light-budget re-measure and freeze, determinism matrix, ledger and rubric, change protocol, repair fail-closed fix). X2 (profiles and registry, byte-equivalent refactor). H1 (Q2 smoke rebuild and profiles, with the coop-1 SP proxy). X3 truth patch. H6 protocol. B1 readers start. **After baseline-0 is recorded:** R1-s entity safety fixes, one cause at a time. These are the only generator-output changes in Phase 2. | Generator lane: X2 (byte-equivalent), then R1-s. Harness: H1, B1. Docs: X3, H6. | Baseline-0 reproduces draft-v0 exactly (70/138; 53/138 with G10 reported). Every gate control passes. `qmg_q2_compact_*` pass. G8 100%. X2 refactor 0 byte diffs. Light budget frozen under 8 × 2. The rebuilt Q2 smoke reproduces 14/14 verdicts, and the coop-1 base1 control shows monsters present. R1-s acceptance test met, each cause with its own baseline-N. Truth patch merged. Build and ctest green on every commit. |
| **Phase 3: truthful intake, runtime-safe modes, visible defects (all 17 targets)** | Rank 1 causes R1-a to R1-h in order (modes, module, mode entities, determinism key last). Rank 2 stage A1 (findings, provenance, trace scaffold, report v5). D-a item catalogue and IR for all three games, plus the allocator extensions (missing weapons, ammo, MH, armor tiers, no Q3 machinegun slot). F1 (Q2 metadata, wall and ceiling roles, skip/caulk, Q1 WAD if supplied, liquid shaders), each measured alone, with the darker Q2 interim recorded behind its switch (D33). G1 probe in report-only mode. H5 analysis-error gate. | Harness: H2 (q3host bind/route calibrated on stock; Q3 smoke once D18 is decided), B1 continues (stock Q1/Q2/Q3 parsed, cohorts, design-history claims measured). | On BENCH-v1 against baseline-N: G2 100% for all 17 targets plus the 12 regressions; G7 100%; G9 0 silent drops; G10 34/34; G11 0 unwaived MP navigation and reachability errors (`unlit_critical_placement` is still reported only); G12 0 missing for Q2/Q3 (and Q1 with a WAD, otherwise Q1 recorded as failing); 0 accidental emitters with the F1 profile on; item-catalogue and item-set completeness rows met for the catalogue; G13 bands predeclared from stock. Q2 smoke passes the DM, TDM and ctf profiles (ctf if D21 is approved) on claimed Q2 MP cases. The coop-1 proxy runs on Q2 SP/co-op cases and is reported, not gated. q3host 100% binding and 0 unexplained failures on stock. |
| **Phase 4: Q2 DM/duel depth, then the Q2 heroes** | Ordered single causes: rank 2 stage A2 (v2 with Q2 DM/duel fields active, including size, mood and landmarks); D budget → solver → spawn policy (Q2), with D-c8/D-c9 defaults; F2 Q2 kits (refinery/industrial, canyon/outdoor) and building rules; the D33 default flip of the Q2 emitter removal, then G1 calibration and rescaling, then G2 (Q2 sources, sun and sky); C1 (multi-level, cliff-wall outdoor volume, landmarks, mechanisms); C5 (variety, ranking). B: q2dm1–8, base1 and q2ctf1–5 measured; Q2 cards extended. Native item-route runs. **Then the Q2 DM hero, the defaults run, and the Q2 duel hero.** | Harness: H2 Q3 smoke and calibration; H4 Q3 pmove started. B1 IBSP46 calibration. | The Q2 DM and duel RC1 sets, MP-PORTFOLIO and SPAN-PORTFOLIO pass every hard gate G1–G13, including G11 `unlit_critical_placement` (G14 under the Q2 profile). D, F, G and C1 acceptance tests met. q2dm1 flagged by D-c1/D-c2 if its clustering was confirmed. Rubric P1–P11 complete for Q2 DM and duel. Q2 DM hero delivered: 15/15 honoured under the recommended D11(5)(a), native routes 100%, smoke, lighting in the stock band, q2dm1–8 comparison. The defaults run passes its test. **Q2 DM frozen here as the RC2 parity reference (D12).** |
| **Phase 5: Q3 paths, then Q2 team and objective modes** | MP sub-lane: C2 (Q3 motifs, pads, patches, bot hints, movement-specific links); C3 (CTF mirroring for Q2 and Q3, base identity, shared-respawn policy D32, TDM team scale, > 8 rooms); D, F2 and G for Q3 (tourney RA/MH/YA control, gametype keys, gothic/base/space kits, q3map_sun); H2 G15 hard; H4 Q3 native; Q3 bot-arena contract under `spmap`; Q2 ctf module smoke; Q2 TDM per D14. B2 knowledge/q3. **Heroes: Q3 FFA, tourney, TDM, CTF, bot arena; Q2 CTF, Q2 TDM**, each declared before generation. | SP sub-lane in parallel: E1 with C6a on the Q2 runtime (needs the D catalogue, R1-s coop starts and D28). Its fixed sets are disjoint from the MP sub-lane's (8.3). | Per path: 100% of its RC1 set, plus its hero, passes G1–G15; rubric parity with the frozen Q2 DM reference; hero package and study; RC4 evidence (Q2 profile smoke; or Q3 smoke plus G15 plus hero bot runs at 100% of items). **E1 checkpoint for Q2 SP and Q2 co-op.** A path that misses is dropped and stated. |
| **Phase 6: Q1 multiplayer (only if D9 WAD and D16 engine are decided by the Phase-3 exit)** | B1 BSP29 calibration on dm1–dm6, knowledge/q1 cards; H3 Q1 smoke (with the deathmatch-forcing check) and H4 Q1 native; C4 Q1 motifs; D for Q1 per D13; F2 Q1 kits with the WAD; G with torches and flames. **Heroes: Q1 DM, Q1 duel** (Q1 TDM only if retained; Threewave only with a runtime). SP sub-lane: E1 for Q1. | — | Same per-path criteria as Phase 5, plus G12 0 missing textures compiled with the recorded WAD. Q1 smoke passes; the match-end check is static unless D20 approves a client. **E1 checkpoint for Q1 SP and Q1 co-op** (8/8 core cases each, engine initialization under H3). |
| **Phase 7: single-player and co-op (go/no-go)** | The E1 checkpoint results from Phases 5 and 6. On "go": E2 (encounters, secrets, objectives, Q2 units and secret levels), C6b progression shape (loop-back, views ahead, arenas), B2 knowledge/sp and stock SP calibration (e1m1–e4m8, base1–3), H coop-1 and Q1 SP profiles. **Heroes: Q1 SP, Q1 co-op, Q2 SP (3-map unit), Q2 co-op**, only for paths kept. | — | Every SP or co-op path that passed its checkpoint (q1-sp, q1-coop, q2-sp, q2-coop) meets every hard gate (G11 hard for SP), the SP rubric, engine initialization at every skill (the coop-1 proxy for Q2 SP), and its hero; otherwise it is dropped and stated. |
| **Phase 8: RC assembly** | Freeze the generator. Full BENCH-v1 run. Re-verify every hero on the final build. Final README/ROADMAP claimed set, drop list and human-playtest status. | — | Every claimed path meets RC 1–5 on the frozen run; `qmg_rc_ledger` and `qmg_docs_claims` green. A `v1.0.0-rc1` tag is created only with the user's approval. |

**Where the Q2 hero sits.** At the end of Phase 4, after A, D, F and G are done for Q2 as the brief requires, plus C1 (three floor levels, cliff-wall outdoor sky, furnace landmark), B1 (q2dm1–8), H1 (reproducible smoke) and D19 (screenshots). It is preceded by a static dry run that maps 15/15 requests to v2 fields. The dry run generates no map, at the hero seed or any other.
- **Prompt.** The brief's exact prompt.
- **Command.** `qmg generate --prompt "<verbatim>" --seed N --format q2-valve220`, and the defaults run `"a Quake II deathmatch map"` at the same seed N.
- **Seed (D26, revised recommendation).**
  - Seed 4242 has already been generated: in draft-v0, and again in every routine BENCH-v1 run, unless it is sealed. Keeping it as the primary would let development see the hero's output at its own seed for three phases.
  - **Primary:** a fresh seed, declared now in HEROES (picked by the user, or by a recorded rule) and present in no fixed set. Nothing is generated at that seed before the hero run.
  - **Seal:** q2-showcase-hero (seed 4242) goes into `sealed.json`. After baseline-0 is recorded, routine runs skip it and check only its prompt through the dry run.
  - **Control:** at the hero run, 4242 is generated too, as a same-seed before/after against draft-v0, and is reported in the study.
  - The primary is never swapped for the control, and neither is re-rolled.
- **Brief resolution.** mode deathmatch, players 2..8 (en dash parsed), compatible_modes [duel] with a typed finding.
- **Honour threshold.** 15/15 honoured, with no request declared out of scope (recommended D11(5)(a); the alternative (b) is ≥ 13/15 with every miss stated). "Cut into a canyon wall" is judged by the cliff-wall trace; "central furnace landmark" by the landmark test and the central verifier; "three floor levels" by height. A miss fails the hero: the generator cause is fixed, the same seed is rerun, and the failed attempt is kept in the study.
- **Duel compatibility** is measured on the same map at 2 players: duel spawn subset in band, D-c5 BA/MH trade-off.
- **Evidence.**
  - static: trace, item-timing simulation, spawn sightlines at +22;
  - compiler: G1–G13, G15 not applicable;
  - native movement: spawn to RG, spawn to Quad, BA to MH, the losing player's recovery, 100% complete with failures kept;
  - engine initialization: H1 DM profile with the buried-item control;
  - engine rendering: furnace landmark, RG, Quad, BA, MH and sky areas, as predeclared views;
  - human play: not done unless sessions are held.

**Where the per-path heroes sit.** Each hero comes at its path's phase exit and is declared in HEROES before its first generation. The hero is the last item before the exit gate, not a separate phase. Draft briefs, for user approval when each path's phase begins:
- **Q2 duel (Phase 4).** A two-level base, RG and RL on opposite sides, BA and MH placed so holding one gives up the other, one teleporter shortcut.
- **Q2 CTF (Phase 5).** Mirrored refinery bases with distinct identities, 3 routes per flag, a contested midfield with a water channel, and shared DM respawn spots placed under D32. Smoked on game ctf and compared with q2ctf1–5.
- **Q2 TDM (Phase 5).** 4v4 with wide main routes, two lifts, team spawn areas far from the Quad. dmflags 64 profile.
- **Q3 FFA (Phase 5).** A gothic arena with jump pads to a high walkway, the Quad far from the RLs, no camping spot overlooking the RA. Compared with q3dm6 and q3dm17.
- **Q3 tourney (Phase 5).** RA, MH and YA in a mutual trade-off, a pad to a railgun ledge. Compared with q3tourney2/4 and their pro- revisions. ztn3tourney1 and hub3aeroq3 are compared only if the user supplies them under D24, since neither is on disk.
- **Q3 TDM (Phase 5).** A space platform over a void, 30 s weapons (GT_TEAM). One multi-gametype map may serve both FFA and TDM only if both studies are written (D26).
- **Q3 CTF (Phase 5).** q3ctf4-style mirrored bases, per-entity flag-to-flag reachability for both teams.
- **Q3 bot arena (Phase 5).** A ladder entry with an `.arena` script and info_player_intermission, 1 player against 3 bots. It is loaded with `spmap` (GT_SINGLE_PLAYER), and its bot-run pickups are read from console output, since g_log is not opened in SP. If the stock SP control fails, D15's fallback applies: the bot runs use FFA, and the SP-specific checks (arena script, intermission, notfree/notsingle filtering) stay static, as the study states.
- **Q1 DM (Phase 6).** A DM3-style power side (RL, Quad) against a recovery side (RA, SNG), medieval kit with torches.
- **Q1 duel (Phase 6).** DM4/DM6 compact verticality with a protected recovery route.
- **Q1 SP and co-op, Q2 SP and co-op (Phase 7, only on "go").**
  - Q1: a castle with silver and gold keys, 3 hinted secrets, knights before ogres, a visible slipgate exit, pistol-start completable at every skill. Co-op hero, under D28 (a): a separate co-op build of the same brief, with 4 coop starts and ammo and health scaled for 4 players. One map cannot do both, because no Q1 spawnflag separates co-op from SP. Weapons stay in coop by progs rule.
  - Q2: a 3-map base unit with $spawnpoint travel both ways, a red key door, a target_help objective and 2 secrets. Co-op hero, under D28 (a): a co-op build of the unit, with starts at each arrival and resources scaled for player_max. One map cannot do both, because NOT_COOP is inert in 3.19.

## 11. Architectural decisions awaiting approval

| # | Decision | Options | Recommendation | Affects |
|---|---|---|---|---|
| D1 | Commit the ~126-change working tree as the RC baseline | (a) One baseline commit on a branch plus a tag. (b) Reviewed grouped commits on a branch: core fixes (Q2 lift/door info_notnull, ramp navigation); compact Q2 generator; tools/map_study and knowledge; recipes and native tools; Phase-1 tools/benchmark labelled separately; then a tag such as `v0.5.0-baseline`. (c) Leave uncommitted. | (b) if review time allows, otherwise (a). Cut it from a quiescent tree. Nothing is rewritten or discarded; out/ stays ignored; benchmark manifests (not outputs) are tracked. (c) leaves fixed sets and evidence unreproducible from a clone. | All; X1 clean-clone test, B, ledger |
| D2 | Report schema v5 | (a) Additive superset of v4: brief, findings, field_provenance, request_trace, unmet list, player range, module, source revision, profile/registry hashes, materials table, light schedule, item_economy. (b) Breaking redesign. | (a). No v4 key changes meaning. tools/benchmark and mp_portfolio readers accept v4 and v5. One bump per phase at most. | X1, ranks 1–5, H, heroes |
| D3 | semantic-plan-v2, with v1 read compatibility | (a) New schema_version 2, GBNF and rules/v2; parser accepts 1 and 2; deterministic v1→v2 upgrade marks new fields defaulted; rules/v1 kept selectable. (b) Extend v1 in place. | (a), with a per-game activation table so unhonourable fields report "unsupported". Approve the schema at the start of Phase 4 from a short design note. That note also fixes the **size** field: bands small 4–6, medium 7–10 and large 11–16 rooms, with footprint caps (provisionally 3072, 4096 and 6144 u) that B calibrates, and derivation from the player range when no size is given. It also fixes the closed mood word list. | Rank 2, C, D, F, G, E |
| D4 | GameMode, team and co-op modes, game module and Q3 gametype vocabulary | GameMode `cooperative`, `team_deathmatch`, `bot_arena`; `GameModule` field in PromptSpec, brief, report and worldspawn `_qmg_module`; CLI `--mode coop|tdm|botarena` and `--module`; Q3 gametype/notfree/notteam keys with the `tournament` (entity) and `tourney` (arena) tokens. Approve all, or a subset. | Approve all. Exhaustive switches with no default. A path without a module decision stays unclaimed. | Rank 1, D, E, H profiles |
| D5 | Determinism key and RNG streams | (a) Key on the canonical normalized-brief hash (raw prompt hash kept one version); per-stage splitmix64 streams; GGUF revision and n_threads in the key. (b) Keep the raw-prompt key and document it. | (a), landed as its own change with a baseline re-record. It changes every layout_sha256. | All generator work, G8 |
| D6 | New pipeline stages | DesignBrief; RequestTrace (after analysis); MaterialResolver + admission; ItemEconomy (replaces round-robin on migrated paths); LightSchedule; lighting analysis before final selection with lighting findings feeding repair; ConstructionPlanner with the three-argument `MapBuilder::build` in the default pipeline; EncounterProgression; repair re-runs the stage chain. | Approve the Phase-3 set now (DesignBrief, RequestTrace, MaterialResolver/admission, repair change). Approve the rest per phase from a short design note. Each lands behind a stage switch (D7). | Ranks 2–7 |
| D7 | Landing pattern for every new behaviour | (a) Stage-profile switch, default off, default output byte-identical; measure on/off; flip in a separate commit; re-record the baseline. (b) Change defaults directly. | (a). It is what makes "one cause at a time" checkable. | All generator work |
| D8 | Layout IR extensions | New PlacementRoles (ammo, powerup, holdable, mega_health, coop_start, intermission, team starts, key, secret trigger, objective, push, fixture light); Placement fields (tier, spawnflags, wait, random, gametype, notfree, notteam, notbot, team); ConstructionRequest types drop, push pad and **LandmarkSpec** (kind, position intent, scale, accent material, light, unique signature), plus the cliff-wall outdoor variant. | Approve. Approve the R1-s roles (intermission, coop_start, team starts) and the D-a item roles and fields now, for Phases 2–3. Approve LandmarkSpec with the C1 design note at the start of Phase 4. Each addition passes the compiler gate as a serialization change and is recorded as its own cause. | D, C, E, G, Q2 hero |
| D9 | Asset and palette registry, Q1 WAD, Q3 shader provenance | (1) Tracked registry of retail texture and shader names, hashes and metadata, no bytes. (2) Q1 WAD: user-supplied with path, licence, author and sha256; or a local-only WAD extracted from the user's own id1 BSPs, never packaged; or none (Q1 dropped). (3) An original `scripts/qmg.shader` (liquids, botclip) with origin, licence, author and hash, or verified stock mappings. (4) Q1 package rule: Q1 BSPs embed texture pixels, so ship `.map` only unless the WAD's licence allows redistribution. | (1) yes, as an explicit D29 exception. (2) A user-supplied WAD, as the brief says. Local-only extraction departs from the brief's wording, so it is used only with the user's explicit approval and a record of origin, licence and hash, and is never packaged. Decide before the Phase-3 exit. (3) An original shader with provenance. (4) `.map`-only Q1 packages unless the licence permits. | F, G, C2, X3, every Q1 path |
| D10 | Game profiles and fact tables as tracked data | `data/profiles` (`qmg.game_profile` v1) compiled into a generated header, with a citation validator. | Approve. The refactor is byte-equivalent first. | X2, D, E, F, G, B |
| D11 | Benchmark v1 as an opt-in CTest gate, and gate policy | (1) `rc-benchmark` label, opt-in and hash-gated, plus a PR-time subset. (2) A contradictory case passes if it generates with a surfaced conflict and documented resolution; refusal only for predeclared unservable combinations. (3) Light budget: provisionally 1200 s (1.5 × the slowest **solo** run). It is frozen in Phase 2 at 1.5 × the slowest time re-measured under the enforced 8 × 2 concurrency, and re-measured once with G2 fixtures and sun in Phase 4. (4) G11 hard set: unreachable placements, flags and spawns from the Phase-3 exit; unlit critical placements from each game's lighting exit; SP monsters from Phase 7; waivers under (8). (5) Honour-rate floor ≥ 85% on claimed paths. For heroes: (a) 100% of the requests not declared out of scope in HEROES before generation, so 15/15 for the Q2 DM hero; or (b) ≥ 13/15 with every miss stated. (6) Unclaimed paths' cases stay in v1 as honesty probes. (7) Q1 CTF scored strictly (team starts) if reinstated. (8) Waiver governance: a closed list of reason codes; each waiver approved by the user and recorded in the ledger; at most 5% of measured instances per gate per path; 0 on heroes unless predeclared. | Accept 1–4 and 6–8 as proposed. For (5), take (a): a hero is the path's showcase, and a planned shortfall belongs in its HEROES entry before generation, not after. | X1, RC1, heroes |
| D12 | RC2 parity bar | (a) Q2 DM as it is today. (b) Q2 DM frozen at the Phase-4 exit. (c) (b) expressed as rubric P1–P11 (plus the SP rubric). | (c). A literal "today" bar would certify round-robin items and black walls as parity. | All paths, ledger |
| D13 | Q1 runtime contract | (a) The shipped 1996 id1 progs.dat (sha256 `f2619787…`; facts from disassembly; weapons, armor, ammo and MH respawn only when deathmatch == 1; sequential spawns). (b) The 2021 re-release QuakeC (source local; MH 120 s fixed; random not-visible spawns; Nightmare player at 50 HP; needs an engine with re-release localization; KEX not local). (c) Entities valid under both, runtime-tested on one. | (c), runtime-tested on the 1996 progs, which any NetQuake-compatible dedicated server runs. Re-release differences are kept as cards and design margins. | D, E, H3, every Q1 path |
| D14 | Q2 runtime contract | Base: (a) 3.19 baseq2 plus 3.19 ctf (source local, matches the pinned module; NOT_COOP inert). (b) The re-release DLL (behaviour not examined). TDM: (i) 3.19 dmflags 64/128 with DM spawns; (ii) re-release teamplay; (iii) OpenTDM (only an x86 build is installed). CTF module: build the 3.19 ctf DLL as x64, or the re-release with ctf 1. | (a) with TDM (i) and an x64 3.19 ctf DLL after source review. Emit NOT_COOP 4096 only if re-release compatibility becomes a goal, recorded as untested. | Rank 1, D, E, H1, Q2 paths |
| D15 | Q3 runtime contract and bot-arena runtime method | 1.32b baseq3 qagame (local qvm `57c52bf2…`, version unverified, to be pinned). Bot arena: (a) a GT_SINGLE_PLAYER ladder entry (`.arena` with map, bots, fraglimit, type; info_player_intermission; notfree semantics); (b) a bot-tuned FFA/tourney map, not claimed separately. Runtime method for (a): (i) `spmap` under GT_SINGLE_PLAYER, with bots from the `.arena` and pickups read from the dedicated console echo, since g_log is not opened in SP (g_main.c:430, 1095-1097); (ii) bot runs under FFA as a stated proxy, with the SP-specific checks static. | 1.32b with the qvm hash pinned; bot arena (a), runtime (i). Fall back to (ii) only if the stock SP control (q3dm1 under `spmap`, no human client) fails, and say so in the study. | Rank 1, C2, H2, Q3 paths |
| D16 | Q1 engine for smoke | (a) Build fteqw-sv from local fteqw-master after a window, input and network source review. (b) Build WinQuake `-dedicated` from id source. (c) The ironwail 0.8.2 binary (source not local, so the review is impossible). (d) An installed unpinned binary. | (a), with NetQuake-compatibility differences recorded; (b) as fallback. Do not pin binaries whose source cannot be reviewed. | H3, H4, Q1 RC4 |
| D17 | Q2 engine pin | (a) Rebuild Q2REPRO at aeeb335f in a documented root and re-pin, keeping the earlier review because the commit is unchanged. (b) A newer commit with a fresh review. (c) Keep using `.agents/tmp` copies. | (a). (c) is not reproducible from a clone. | H1, every Q2 path |
| D18 | Q3 host and window focus | (a) Run Quake3e dedicated as built, accepting that its console takes focus (win_syscon.c:871), only in sessions the user attends. (b) Build Quake3e from the local checkout with a minimal reviewed change that suppresses focus, as a separate GPL binary. (c) Review another installed host (ioq3ded and others, unpinned). | (b) for unattended Q3 smoke and bot runs; (a) with per-session consent until it exists. | H2, Q3 RC4 |
| D19 | Windowed clients for hero screenshots (all three games) | (a) Per-session approval: windowed client, the engine `screenshot` command, camera placed by console commands, no input. (b) Reviewed patched clients that do not take focus. (c) No screenshots; studies state the absence (fails the hero deliverable as written). | (a) now; (b) later if focus theft is unacceptable. `+forward`-style commands count as input and are not used. | F, G, X3 |
| D20 | Connected-player runtime | Approve or forbid: server-side Q3 bot runs (addbot, no client); a Q1/Q2 loopback client for intermission, spawn selection, changelevel and $spawnpoint tests. | Approve Q3 bot runs. No Q1/Q2 connected client in rc1: those results stay static evidence and the README says so. | Rank 1, E, H |
| D21 | Separate-process GPL builds | An x64 3.19 ctf DLL; `tools/q3host` (botlib + cm); the Q1 and Q3 native hosts; a Q1 server (D16); FTEQCC to compile quakec_ctf (needed only if Threewave is kept). | Approve the first four after source review. Defer FTEQCC. | H, C2, Q2 CTF |
| D22 | New output artifacts | A Q3 `.arena` script beside the `.map`; Q2 multi-map unit output (a unit manifest plus several `.map` files). | Approve `.arena` in Phase 3. Decide unit output at the E1 checkpoint. | Rank 1, E |
| D23 | Ranker feature change | `feature_count` goes from 8 to N (construction, route, exposure, item-control features). It changes selection for every seed. | Approve, one feature at a time with ablation records, abstaining where evidence is lacking. Weights stay hand-set; none is fitted to reference maps (D30). | C5, D |
| D24 | Knowledge schema v2 and reference-map policy | Card schema v2 (v1 readable, no deletions); stock id maps as local-only study references in their own provenance class (`training_eligible=false`); the community maps ztn3tourney1 and hub3aeroq3, which the brief names, as local-only measurement references. They are **not on disk**: a file-name search of E:/_SOURCE, E:/Games and E:/Development found only a Q2 conversion, q2ztn3tourney1. They could be used once the user supplies them with origin, author, licence and hash. | Approve schema v2 and stock now. For the community maps: the user either supplies them, and they are approved for measurement once their readme terms are recorded, or they are left out and the Q3 tourney study says so. Never copy geometry. | B, D-c5, Q3 tourney hero |
| D25 | RC4 if no Q1 path qualifies | (a) RC4 applies to the claimed games; rc1 ships as Q2/Q3 with Q1 listed as unclaimed and experimental. (b) Hold rc1 until at least one Q1 path qualifies. | Decide now, so effort is planned. (a) is the honest default if the Q1 inputs cannot be supplied. It reads the brief's "all three games" as "every claimed game", which departs from the brief's wording, so only the user can accept it. Under (a), the RC statement says plainly that Q1 has no engine smoke. | Scope, Phase 6 |
| D26 | Hero precondition, seeds, seal and briefs | Q2 hero after A/D/F/G only, with floors and sky as typed unmet; or also after C1. Seed: (a) keep 4242 as the primary and seal q2-showcase-hero from routine runs; (b) a fresh primary seed declared now and found in no fixed set, with q2-showcase-hero sealed and 4242 generated only at the hero run, as a same-seed before/after control. Briefs: user-written, or drafted for approval. One multi-gametype Q3 hero for FFA and TDM, or one per path. | After C1. Seed (b): 4242's output is already known from draft-v0, and without a seal it would be regenerated in every BENCH-v1 run through Phases 2–4. Never re-roll. Briefs drafted for user approval at each path's phase. A shared FFA/TDM map only if both studies are written; separate heroes for tourney and CTF. | X3, Phase 2–7 |
| D27 | Human playtesting | (a) The user runs protocol sessions for heroes (hashes, settings, demos, anchored ratings). (b) No sessions; README/ROADMAP say so plainly. | Write the protocol either way. Default to (b) and state it, unless the user schedules sessions. | H6, RC5 |
| D28 | Co-op output model | Neither Q1 nor Q2 3.19 has a coop-only spawnflag. Q1 skill flags apply to SP and coop alike (WinQuake pr_edict.c:931-948), and Q2 NOT_COOP is inert (g_spawn.c:587). Items do not respawn in coop, and weapons stay. Options: (a) a co-op prompt produces a separate **co-op build**, with resources for player_max baked in and coop starts; it is not claimed as balanced SP. (b) One shared SP/co-op map with no resource scaling; co-op adds starts only, and the tests and hero briefs drop "scaled". (c) Scaling through custom progs or a DLL (out of scope). | (a) for the co-op paths. Shared maps from SP prompts carry a typed `coop_no_scaling` finding. Decide by the Phase-4 exit, before E1 starts. | E, rank 1 (co-op starts unaffected), co-op heroes |
| D29 | Stock-derived numbers in tracked files | The plan's Locality test bans map-derived outputs under tracked paths, but several deliverables would track stock-derived numbers: cohort profiles, stock measurement tables, card `measure` fields, the asset registry. (a) A permitted-aggregate rule: tracked files may hold scalar aggregates (bands, medians, counts) and texture/shader names, hashes and metadata. Each records its source map ids and script hash, and may live only in approved locations (`data/profiles`, `data/assets`, `knowledge/**`, `tools/benchmark/calibration`). Geometry, entity lumps, lightmaps, image bytes and per-map raw outputs are never tracked. (b) Nothing stock-derived is tracked: aggregates stay under `out/`, and only their hashes are tracked. | (a), with the Locality test checking the aggregate schema and locations. This is an exception to "reference-map outputs stay local", and needs the user's explicit approval. Decide before Phase 2, since X2 and B1 write these files. | X2, B, D, F, G, Locality test |
| D30 | Calibration versus training | Generator budgets and thresholds are "calibrated on B cohorts", and D23 adds ranker features. (a) Calibration is descriptive: bands are percentiles or medians chosen by a recorded rule and reviewed by a person. No parameter is fitted by optimisation against reference maps, and ranker weights stay hand-set (src/ggml_ranker.cpp:126) until a rights review. (b) Treat any use of stock statistics as learning, and wait for the rights review. | (a), stated in DATASET.md and ROADMAP M4, so that "no training until rights are reviewed" stays true and checkable. | B, C5, D, D23 |
| D31 | Q2 SP engine-evidence route | A dedicated server forces deathmatch 1 unless coop is set (sv_init.c:318-324; q2repro-current init.c:437-442), so "deathmatch 0" SP initialization is not possible. (a) Initialize SP maps under coop 1 as a stated SP proxy: monsters spawn and skill filtering matches SP, because NOT_COOP is inert, but start selection and weapon stay differ. (b) A windowed listen server (D19/D20), which is a client. (c) SP stays static evidence. | (a), labelled "engine initialization (coop-1 proxy)" everywhere it is cited. Add (b) only if the user approves a client under D20. Decide before H1 in Phase 2. | H1, E, Q2 SP and co-op paths |
| D32 | Q2 CTF respawn-spot policy | In Q2 CTF, every respawn after the first uses team-agnostic info_player_deathmatch spots (ctf/g_ctf.c:449-455). Threewave respawns at team spawns and uses DM spots only as a fallback and for runes (quakec_ctf client.qc:535-547), so the policy matters there only for the rune and fallback spots. (a) Place the shared DM spots in neutral and midfield zones, balanced between halves, each at least 0.3 × the flag-to-flag path length from both flags, with equal mean path time to each flag within 10% (provisional). (b) Accept DM spots anywhere and measure only. | (a), calibrated on q2ctf1–5 before it gates. Decide by the start of Phase 5. | D-d, C3, Q2 CTF, Q1 CTF |
| D33 | When the F1 Q2 emitter removal becomes the default | Removing the accidental emitters (explicit metadata and the ceiling replacement) drops the mean Q2 lightmap from about 152 to about 20, below the stock 45–73, until G rescales light in Phase 4. (a) Flip the default in Phase 3 and ship the dark interim for a phase. (b) Land and measure it behind its switch in Phase 3, and flip it in Phase 4 in the commit just before the Q2 light rescaling flips. Each flip stays its own measured cause. | (b). The one-cause rule is kept, and default Q2 output never spends a phase below the stock band. Phase-3 exit rows for emitters are measured with the F1 profile on. | F1, G1, Phase 3–4 exits |
| D34 | Evidence-level names beyond the brief's five | The brief names static analysis, compiler output, native movement trial, engine initialization and human play. This plan also uses **AAS routing** (pinned id botlib run in q3host over a compiled AAS; no bot, no frame), **engine runtime with bots**, and **engine rendering**. Options: (a) adopt the three names as levels between the brief's; (b) fold AAS routing into native movement trial and the other two into engine initialization. | (a). Folding them would overstate (AAS routing is not a movement trial) or understate (bot runs exceed initialization). Each result keeps exactly one level. | H2, G15, RC4, every study |

## 12. Release-candidate scope

**Principle.** rc1 claims only the paths that reach the parity bar (Q2 DM frozen at the Phase-4 exit, rubric P1–P11), with every hard gate, a hero and RC4 evidence. A smaller honest rc1 is preferred over a full-width one shipped weaker. There is no evidence yet for how many paths will qualify. As a planning assumption only, effort is sized for the 6 committed paths plus the conditional ones whose inputs the user supplies. Today no path meets all five RC conditions, RC3 and RC5 are unmet everywhere, and Q2 DM's engine evidence does not reproduce from its documented root.

**RC4 as measured.** RC4 for Q2 is the profile smoke (engine initialization; the coop-1 proxy for SP). For Q3 it is the smoke, plus G15 (AAS routing, deterministic, gating), plus hero bot runs at 100% of items (engine runtime with bots, gating for the hero only). For Q1 it is H3, if Phase 6 runs. Whether RC4's "all three games" can be read as "every claimed game" is D25. That reading departs from the brief's wording, and needs the user's decision.

| Path | Recommendation | Reason | What would change it |
|---|---|---|---|
| Q2 DM | **Keep** (committed) | 8/8 hard gates; the deepest construction and native-movement evidence (131 native trials); the brief designates its hero. Its engine evidence is the only engine evidence in the project: dedicated initialization on 14 studies, of which only 6 are default-generator BSPs, all predating the current build. That evidence does not reproduce from its documented root. The remaining work (A, D, F, G, C1, H1) is known and measurable. | Only a failure to reproduce the Q2 smoke (D17) or to meet its own Phase-4 exit. |
| Q2 duel | **Keep** (committed) | 7/8; shares the compact path and the runtime. Needs duel item control, spawn count and its own hero. | Same as Q2 DM. |
| Q3 FFA | **Keep** (committed) | 8/8; the contract is fully sourced (1.32b); engine, qvm, botfiles, BSPC and stock AAS are local. The gap is construction depth, the item model and the per-entity checker. | q3host calibration fails on stock (G15 cannot be trusted), or D18 leaves no runnable host. |
| Q3 tourney | **Keep** (committed) | 2/8 is a vocabulary miss. The item-control layer is shared with Q2 duel. The stock references (q3tourney1–6, pro-q3tourney2/4) are local. The community maps the brief names (ztn3tourney1, hub3aeroq3) are not (D24). | Same as Q3 FFA. |
| Q3 TDM | **Keep** (committed) | The contract is unambiguous (GT_TEAM, DM spawns, 30 s weapons): gametype keys on top of FFA depth. | Same as Q3 FFA. |
| Q3 CTF | **Keep** (committed) | 8/8 gates; entity contract matches 1.32b; q3ctf4 is local; mirrored-base work is shared with Q2 CTF. | C3 cannot meet mirror plus 3-route fairness at the claimed sizes. |
| Q2 CTF | **Keep if input supplied** | The entity contract matches the official module and the references are local. It needs mirrored construction, a respawn-spot policy for the team-agnostic DM respawns (D32), and an x64 ctf module the smoke can load. | D14 and D21 approved, so it becomes committed; refused, so drop. |
| Q2 TDM | **Keep if input supplied** | The map contract is close to DM. It needs the contract choice, a team-scale layer above 8 rooms, and the light budget. | D14 chosen, so committed; if TDM adds no measurable team-scale depth, document it as a use of Q2 DM instead. |
| Q3 bot arena | **Keep if input supplied** | The runtime is local; it needs a definition and an emitted SP contract (`.arena`, intermission). It must be loaded with `spmap`, and g_log is not opened in SP, so its runtime evidence uses the console-echo method (D15 (i)). | D15 (a) gives it its own contract; (b) folds it into FFA, so it is not claimed separately. If the stock SP control fails, its bot evidence is an FFA proxy, and the study says so. |
| Q1 DM | **Keep if input supplied**, by the Phase-3 exit | The map contract is simple and dm1–dm6 plus id source are local, but without a WAD the output cannot render and without a reviewable engine RC4 fails. | D9 WAD and D16 engine decided by the Phase-3 exit, so keep; otherwise defer to after rc1 per D25. |
| Q1 duel | **Keep if input supplied** | Rides on Q1 DM plus the shared duel item-control layer. | Same as Q1 DM. |
| Q1 SP | **Decide at the Q1 E1 checkpoint, at the Phase-6 exit** (default: defer) | Passes 8/8 only at compiler level with textures masked. It needs all of E plus Q1 inputs, and has no runtime evidence. | E1 passes on its 8 core cases after the Q1 inputs arrive, so keep; otherwise drop and state it. |
| Q1 co-op | **Decide with Q1 SP** | Cannot get ahead of SP. Adds coop starts, and under D28 (a) a separate co-op build with scaled resources. | Follows Q1 SP, plus D28. |
| Q2 SP | **Decide at the Q2 E1 checkpoint, at the Phase-5 exit** (default: defer) | Best Q2 tooling, but needs all of E plus unit structure. A dedicated server cannot initialize it as SP, so its engine evidence is the coop-1 proxy (D31). $spawnpoint arrival and changelevel stay static without a client (D20). | E1 passes, and unit travel validates statically plus coop-1 initialization per map. |
| Q2 co-op | **Decide with Q2 SP** | Cannot get ahead of SP. | Follows Q2 SP, plus D28. |
| Q1 TDM | **Drop as a separate claim** | id1 teamplay is a server cvar with no map-side contract beyond DM. A separate hero and study would measure nothing new. Document Q1 DM as teamplay-capable instead. | Q1 DM is kept and the C3 team-scale layer transfers with measurable team-specific depth. |
| Q1 CTF via Threewave | **Likely drop** | No runnable runtime: no compiled quakec_ctf progs, no QC compiler built, only the 5.0 re-release source, KEX not local, port support for re-release localization unverified. It also needs mirrored construction that no game has yet. | The user supplies a working Threewave 5.0 runtime (compiled progs plus a known-good engine) before Phase 6, or approves building it with FTEQCC (D21). |

Every dropped or deferred path is named in README and ROADMAP with its reason and the evidence level it reached.

## 13. What this plan will not claim

- **No human play.** No human has played any generated map. Unless the user runs protocol sessions (D27), every study and the README will say "No human playtest of any generated map has been done." Ratings are recorded as null when not observed.
- **Static analysis is not play.** The analyzer verdicts are static models: spawn safety, item control, timing simulation, pistol-start completability, qualifier verdicts. Zero spawn-pair LOS does not prove respawns are safe. A pistol-start simulation has no monster AI, damage variance or player skill.
- **Compiler output is not appearance.** The G13 lighting gate measures stored lightmap bytes at sample points, without overbright, gamma or rendering. Only engine screenshots are rendering evidence, and those are reviewed by the user, not measured.
- **Engine initialization is not a match.** A dedicated server with no connected player shows that entities spawn, that nothing starts in solid or drops, and that the module accepts the entity contract. It does not show spawn selection, pickups, mover use, flag capture, intermission, changelevel or $spawnpoint arrival. Unless D20 approves a client, the Q1 match-end fix and the Q1/Q2 spawn-selection fixes stay static evidence, and the docs say so.
- **A Q2 SP map is never engine-initialized as SP.** A dedicated server forces deathmatch 1 unless coop is set, so Q2 SP evidence is "engine initialization (coop-1 proxy)" (D31), and is always cited with that label.
- **AAS routing is not bot behaviour.** G15 shows that botlib can route between entities (AAS routing, an added level, D34). Bot runs show that bots touched items in K fixed runs; bot runs are not deterministic and gate only heroes, not the benchmark. Neither shows match quality.
- **A protocol is not play.** PLAYTEST_PROTOCOL.md and its schema are documents checked by static analysis. They produce no human-play evidence.
- **Native movement trials are world-only.** They exclude weapons, damage and other players, so rocket-jump routes are not proven by them. Those routes are static analysis only and are never required. Strafe-jump routes are native trials with scripted command sequences, not human input.
- **Calibrated thresholds are descriptive.** Bands come from small stock cohorts (some marked insufficient). They describe stock maps; they do not establish that a map is good, and they are not trained parameters (D30). Every card stays a `design_hypothesis` until measured, and none is promoted on human evidence. Thresholds marked provisional, such as the qualifier percentiles, the 5% waiver cap and the spawn counts, are project choices until B replaces them.
- **Facts carry their basis.** 1996 Q1 facts come from disassembly. KEX re-release engine behaviour, Threewave 4.x, the qagame.qvm version, the Q2 re-release DLL, fteqw-sv's deathmatch handling, and whether Q3 SP bots start without a human client are unverified until checked. The design-history claims in the brief (q2dm1 clustering, dm3 sides, dm2 trap) are measured before use.
- **Determinism scope.** Byte determinism is claimed for a fixed normalized brief, model revision, `n_threads` and seed on this machine and toolchain. Cross-machine float determinism of the GGUF path is not claimed.
- **No result is claimed above the evidence level it reached.** Each result in the ledger, studies and README carries its level, and each failure and counterexample stays in the record.

## 14. Inputs needed from the user

1. Approval of this plan and its phase order, including the entity safety fixes (R1-s) in Phase 2 and the item catalogue (D-a) in Phase 3. Approval of the Phase-2/3 architectural set: D2, D4, D5, D6 Phase-3 subset, D7, the D8 subset, D10, D11 (including the hero honour threshold (5) and waiver governance (8)), D12.
2. The baseline decision for the ~126 uncommitted changes (D1): grouped commits, one commit, or leave uncommitted.
3. Game contracts: Q1 1996 or re-release progs (D13); Q2 3.19 or re-release, the TDM contract and the CTF module (D14); the meaning of Q3 bot arena, its runtime method, and permission to pin qagame.qvm (D15).
4. A Q1 texture WAD (path, licence, author, sha256), and the Q1 package rule (D9). The brief says the user supplies it. A local-only extraction from id1 BSPs would depart from that and needs explicit approval. Needed by the Phase-3 exit if Q1 MP is to stay.
5. Engine and host choices: Q1 smoke engine (D16), Q2REPRO rebuild at aeeb335f (D17), Q3 host and its focus behaviour (D18), and approval of the separate-process GPL builds (D21).
6. The windowed-client policy for hero screenshots in all three games (D19), and whether Q3 bot runs and any Q1/Q2 loopback client are allowed (D20). The Q2 SP engine-evidence route, with the coop-1 proxy recommended (D31).
7. The claimed-set tiers in section 12, and how RC4 is read if no Q1 path qualifies (D25).
8. Hero decisions: the Q2 hero precondition (after C1); the seed (recommended: a fresh primary seed declared now, q2-showcase-hero sealed, 4242 as a control at the hero run); the hero name; approval of each per-path brief when its phase starts (D26).
9. The reference-map policy: stock id maps as local-only study references (D24). Whether you will supply ztn3tourney1 and hub3aeroq3, which are not on disk, or whether the Q3 tourney study goes ahead without them.
10. Whether human playtest sessions will be run, and by whom; otherwise the docs state that none were done (D27).
11. Before Phase 2: whether stock-derived aggregates may be tracked (D29), and the calibration-not-training rule (D30). Also the timing of the Q2 emitter-removal default (D33) and the evidence-level names (D34).
12. By the Phase-4 exit: the co-op output model (D28). By the start of Phase 5: the CTF respawn-spot policy (D32).
