# Human playtest protocol

Written 2026-10-15 as workstream H6 of the [release-candidate plan](RC_PHASE1_AUDIT_AND_PLAN.md) (section 9.10). It says how a human playtest of a generated map is prepared, run, recorded and entered in the results ledger.

**This is a protocol, not a result.** The document and its [session-record schema](../tools/benchmark/schemas/playtest-session.schema.json) are checked by static analysis only. They produce no human-play evidence. No session has been run under this protocol, and no human playtest of any generated map has been done. Human-play evidence exists only once a session record that validates against the schema has been entered in the ledger with the user's approval (section 14).

The protocol builds on the reference-map session in [MAP_STUDY.md](MAP_STUDY.md) ("A session that produces useful knowledge") and on the session template that the study tools write to `out/map-study/study-plan.template.json`. Its anchored ratings are the same, and section 13 maps every template field to the new record.

## 1. Scope

- **Maps.** Generated maps first: each path's hero, then benchmark cases chosen before the session. A stock id map of the same game and mode may be added as a control condition (section 5). Stock maps are local-only references; nothing from them is copied into the project.
- **Paths.** All 17 game × mode paths have a question set (section 8). A path the generator cannot produce today (co-op, team deathmatch, Quake Threewave CTF, Quake III bot arena) can be played only on a stock control map until that mode exists. Such a session tests the protocol, not the generator.
- **What it measures.** What people do and report while playing: getting lost, dying at spawn, controlling items, finding secrets, recognising places, finishing a level. It does not measure frame time, network quality or rendering accuracy.

## 2. Before a session

1. **The user approves the session.** A session launches a game client, which takes window focus. The user approves each session, its date and its participants beforehand, and the approval is recorded (`user_approval`). During a playtest, no agent or script launches a client, joins a server or sends input to a game.
2. **Pick the maps and conditions in advance.** Write the maps, conditions, player loads, time boxes and landmark points (`landmark_points[]`) into a record with `status: "planned"` before anyone plays. Do not change them after seeing the map in play; if a change is needed, record it under `deviations`.
3. **Fix the exact files.** Compute the hashes listed in section 6 and check them again on the machine that hosts the session. A rebuilt map is a different map, even from the same prompt and seed.
4. **Check the mode that actually runs.** Engines can change the mode silently. A Quake II dedicated server forces `deathmatch 1` unless `coop` is set (Quake-2-master/server/sv_init.c:318-324), and Quake III resets `g_gametype 2` to free-for-all under `map` (Quake-III-Arena-master/code/server/sv_ccmds.c:179-181), so a bot-arena entry is loaded with `spmap`. At the start of each condition, confirm the mode in the game itself (monsters present in single-player and co-op; the scoreboard or server info shows the game type), and write what was checked in `runtime.mode_check`.
5. **Prepare the record directory.** Records, demos, screenshots and captured settings go under `out/playtest/<session_id>/`. `out/` is ignored by Git. Nothing from a session is committed or packaged.

## 3. Roles

| Role | Responsibilities |
| --- | --- |
| Lead | Runs the session plan. Records consent, settings and hashes. Starts and stops recording. Reads the questions out at the fixed points. Records deviations. |
| Player | Plays the conditions and answers the questions. Does not see the map's report, overview or study before the session. |
| Recorder | Writes observations with times and entity or space ids while play runs. Does not play in the same condition; if that cannot be avoided, mark the observations `player_report`. |
| Observer | Optional. Watches without playing and adds observations. |
| Reviewer | Rates the map after reviewing demos and observations (section 10). For a blinded comparison, at least one reviewer must not have prepared the variants. |

One person may hold several roles; the record lists every role each participant held. Participants are identified only by pseudonymous ids (P1, P2, …).

An agent may prepare a planned record, compute hashes, and parse demos after the session. It never fills in answers, ratings or consent on a person's behalf, never launches a client and never controls input.

## 4. Consent and personal data

- **Before any recording,** the lead explains what is recorded and asks each participant to agree to: taking part; game demos; engine screenshots; written notes. Each item is recorded separately under `participants[].consent`, with the date and who recorded it. No audio or video of people is part of this protocol.
- **Pseudonyms.** Demos store in-game player names, so each participant plays under their pseudonymous id. Records hold no real names, contact details, addresses or IP addresses.
- **Withdrawal.** A participant can withdraw at any time, during or after the session. Their demos, answers, events, observations and ratings are then deleted, and the record keeps only their id with `withdrawn: true` and every other field of that participant empty or null. This overrides the project rule that failures and counterexamples are never deleted. If the withdrawal leaves a condition with too few players, that condition is marked as not usable under `deviations`.
- **Storage.** Session data stays on the local machine under `out/playtest/`. It is not shared, uploaded or packaged unless every participant in that session agrees in writing.
- **Participants under 18** take part only with a parent's or guardian's consent, recorded with theirs.

## 5. Session plan

A first pass on one map takes about 50–60 minutes, as in MAP_STUDY.md. Repeat at different loads in separate runs rather than stretching one session. Sessions longer than 90 minutes include a break.

| Step | Minutes | What happens |
| --- | --- | --- |
| Provenance check | 5 | Check the hashes, settings, module and player count against the planned record. Capture the server settings to a file. |
| Spatial reconnaissance | 10 | Each player walks the map once without opponents and names the spaces they pass, in their own words. |
| Spawn and item audit | 10 | Multiplayer: start from each spawn in turn and note the first useful weapon, the first safe choice and the first contested space. Single-player: skip this step; the first run is the audit. |
| Play conditions | 15–20 per condition | Play the planned conditions with recording on (loads below). Single-player: one pistol-start run per planned skill, each time-boxed. |
| Review | 10 | Players answer the mode questions. The recorder links each answer to observations. Unobserved items stay null. |

**Player loads per mode.** Each condition records its own player count.

| Mode | Loads | Conditions |
| --- | --- | --- |
| Single-player | 1 player | One pistol-start run per skill tested (0–3). Pistol start means the game's default starting inventory: a new game, not a level change from an earlier map. Record the exact load command and the time box. |
| Co-op | 2 and 4 players, in separate runs | `coop 1`, the same skill for every player. |
| Deathmatch / free-for-all | The lower and upper ends of the player range the prompt asked for, in separate runs | If the prompt gave one count, play at that count; if it gave none, at the number of deathmatch spawns. Record that only one load was tested. |
| Team deathmatch | 2v2 and the team size the prompt asked for | Swap sides at half time. |
| Duel / tourney | 2 players | Exchange starts and roles between runs; at least two runs. |
| CTF | At least 3v3, and the team size the prompt asked for if larger | Swap sides at half time. |
| Quake III bot arena | 1 player against the bots named by the arena script | Load with `spmap`. Record each bot's name and skill. |

**Control condition (optional).** A stock map of the same game and mode, played at the same load and settings, rated with the same anchors. It cannot be blinded, because stock maps are recognisable; record that.

## 6. Settings and hashes to record

Record every value even when it is the default. A value that was not checked is null.

**Files** (sha256, lowercase hex):

| Field | What |
| --- | --- |
| `map.map_sha256` | The generated `.map` (null for stock maps) |
| `map.bsp_sha256` | The exact BSP loaded |
| `map.report_sha256` | The `.qmg.json` report (generated maps) |
| `map.aas_sha256`, `map.arena_sha256` | Quake III `.aas` file and `.arena` script, when present |
| `map.prompt_sha256`, `map.seed` | The UTF-8 prompt bytes and the seed (generated maps) |
| `build.qmg_sha256`, `build.generator_version` | The `qmg` executable and the version in its report |
| `build.compiler[]` | Each compiler tool, its sha256 and its exact arguments |
| `runtime.engine` | Engine name, version and executable sha256 |
| `runtime.game_module` | The game logic actually loaded: Quake `progs.dat`, Quake II game DLL, Quake III `qagame` |
| `protocol.sha256` | This document as used in the session |

**Server settings.** Save the engine's own settings output to `runtime.settings_capture` and copy the values below into `runtime.settings`.

| Game | Settings |
| --- | --- |
| Quake | `deathmatch`, `coop`, `teamplay`, `skill`, `fraglimit`, `timelimit`, `noexit`, `samelevel`, `sv_gravity`, `sv_maxspeed` (Quake-master/WinQuake/host.c:63-78, sv_phys.c:44, sv_user.c:167) |
| Quake II | `deathmatch`, `coop`, `dmflags`, `skill`, `fraglimit`, `timelimit`, `maxclients`, `game` (the module directory), `capturelimit` under the CTF module, `sv_gravity` (Quake-2-master/server/sv_main.c:950-958, qcommon/files.c:871, game/g_save.c:164, ctf/g_save.c:192) |
| Quake III | `g_gametype`, `fraglimit`, `timelimit`, `capturelimit`, `g_speed`, `g_gravity`, `bot_minplayers`, `g_synchronousClients` (Quake-III-Arena-master/code/game/g_main.c:110-143; g_bot.c:974) |

Also record: windowed mode (always on), network (`loopback`, `lan` or `internet`), and for each bot its name and skill.

## 7. Recording demos and screenshots

- **Demos.** Record one demo per player per condition where the engine allows it, and hash each file. The commands differ by game:
  - Quake: `record <demoname> <map>` starts the map and the recording together. With only a demo name, recording is refused once the client is connected (Quake-master/WinQuake/cl_demo.c:193-240).
  - Quake II: `record <demoname>` works only once the player is in a level (Quake-2-master/client/cl_main.c:160-184).
  - Quake III: `record <demoname>` works only in a level and warns unless `g_synchronousClients 1` is set (Quake-III-Arena-master/code/client/cl_main.c:256-285).
  Demo files are copied to `out/playtest/<session_id>/demos/` before they are hashed.
- **Screenshots.** Only the engine's own `screenshot` command (or Quake III `screenshotJPEG`, Quake-III-Arena-master/code/renderer/tr_init.c:1003-1004) in a windowed client, never an operating-system capture. Each screenshot records its view (for example "red base from the flag") and its hash.
- **Times.** Event and observation times are seconds from the start of the condition's demo. When a time comes from the recorder's notes instead of a demo, the source says so.

## 8. Questions per mode

Every session asks the questions for **all modes** plus the set for its path's mode. Answers go to `answers[]` with the question id. Countable events (deaths, pickups, captures) go to `events[]`, so they can be recounted from demos. A question that was not observed is answered null.

**Path to question set.**

| Path | Question sets |
| --- | --- |
| `q1-sp` | all, sp |
| `q1-coop` | all, coop, sp |
| `q1-dm` | all, dm |
| `q1-duel` | all, duel |
| `q1-tdm` | all, tdm |
| `q1-ctf-threewave` | all, ctf |
| `q2-sp` | all, sp |
| `q2-coop` | all, coop, sp |
| `q2-dm` | all, dm |
| `q2-duel` | all, duel |
| `q2-tdm` | all, tdm |
| `q2-ctf` | all, ctf |
| `q3-botarena` | all, botarena |
| `q3-ffa` | all, dm |
| `q3-tourney` | all, duel |
| `q3-tdm` | all, tdm |
| `q3-ctf` | all, ctf |

### All modes

| Id | Question | Recorded as |
| --- | --- | --- |
| `all.landmark_where` | "Where are you?" The lead asks it when a player respawns at, or first reaches in that condition, a predeclared point, and the player answers in a few words without a scoreboard, overview or map. Points are fixed in `landmark_points[]` before the session: each spawn or start, each major item, each objective, and the map's declared landmarks. | `landmark_checks[]`: declared point id, intended name, answer, recognised (true, false or null) |
| `all.lost` | Did you get lost? Where, and for how long? | Text, plus `lost` events with space ids |
| `all.movement_failure` | Where did you get stuck, miss a jump, fall unexpectedly or find a doorway blocked? | `stuck` and `fall` events, observations |
| `all.readability` | Where could you not see the route, an opponent or an item? | Text with space ids; screenshots if taken |

### Single-player (sp)

| Id | Question | Recorded as |
| --- | --- | --- |
| `sp.pistol_start_completion` | Per skill: was the level finished from a pistol start within the time box? How many deaths, and where? | One answer per skill (true, false or null), `death` and `exit_reached` events |
| `sp.secrets_found` | How many secrets were found, out of how many the map has? | Count; null if the run ended early |
| `sp.secret_hints_noticed` | For each secret, found or not: did the player notice its hint? | One answer per secret |
| `sp.keys_and_locks` | Did the player understand which key opens which door? How long were they blocked? | Text and seconds |
| `sp.resources` | Did ammunition or health run out? Where? | Text with space ids |
| `sp.encounters` | Which fights felt unfair or trivial, and why? | Observations; interpretation kept separate |
| `sp.exit_recognised` | Was the exit recognised by sight before it was reached? | true, false or null |

### Co-op (coop)

| Id | Question | Recorded as |
| --- | --- | --- |
| `coop.start_spread` | Did the players start at separate spots? Was anyone telefragged at the start? | `telefrag` events, text |
| `coop.respawn` | Where did a player reappear after death, and how far from the group? | Text with space ids |
| `coop.blocking` | Did one player's action block another (a door closed, a lift taken, a key held)? | Observations |
| `coop.resource_sharing` | Were there enough weapons, ammunition and health for the group? | Text |

### Deathmatch and free-for-all (dm)

| Id | Question | Recorded as |
| --- | --- | --- |
| `dm.spawn_deaths` | From demos: every death within 10 s of a respawn, with the spawn and the seconds since spawning. The 10 s window is a recording window, not a pass threshold. | `spawn_death` events |
| `dm.item_control` | Which major items were contested? Did one player hold them for long stretches? | `pickup` events, text |
| `dm.routes` | Which routes were used between the main spaces? Which spaces were never used? | Text with space ids |
| `dm.dead_travel` | Where were there long stretches with nothing to do? | Text with space ids |
| `dm.congestion` | Where did players pile up or block each other? | Text with space ids |

### Team deathmatch (tdm)

| Id | Question | Recorded as |
| --- | --- | --- |
| `tdm.spawn_deaths` | As `dm.spawn_deaths`. | `spawn_death` events |
| `tdm.team_spawn_safety` | Did players respawn near the other team or inside a fight? | Text, `spawn_death` events |
| `tdm.item_control_by_team` | Which team held which major items, and for how long? | `pickup` events, text |
| `tdm.side_fairness` | Scores before and after the side swap. | One answer per half, scores per side as text |

### Duel and tourney (duel)

| Id | Question | Recorded as |
| --- | --- | --- |
| `duel.spawn_deaths` | As `dm.spawn_deaths`. | `spawn_death` events |
| `duel.item_timing` | Did the players time the major items? Pickup intervals from demos. | `pickup` events, text |
| `duel.loser_recovery` | After losing control of the major items, how long and by which route did the losing player recover? | Seconds, text with space ids |
| `duel.start_fairness` | From each start, how long to the first useful weapon? Compared after exchanging starts. | Seconds per start |

### Capture the flag (ctf)

| Id | Question | Recorded as |
| --- | --- | --- |
| `ctf.carrier_routes` | Which routes did flag carriers take, and how often each? | `capture` and `flag_return` events, text with space ids |
| `ctf.side_swap_fairness` | Captures per side before and after the side swap. | One answer per half, captures per side as text; `capture` events |
| `ctf.base_defence` | Where did defenders stand? Could attackers enter the base by more than one route? | Text with space ids |
| `ctf.respawn_distance` | How long from a respawn to the player's own flag and to the enemy flag? | Seconds |

### Quake III bot arena (botarena)

| Id | Question | Recorded as |
| --- | --- | --- |
| `botarena.bot_item_reach` | Which items did bots pick up, and which did they never touch? | `pickup` events |
| `botarena.bot_stuck` | Where did bots get stuck or wander? | `stuck` events with space ids |
| `botarena.match_end` | Did the match reach its limit and show the intermission view? | true, false or null; `match_end` event |
| `botarena.bot_pressure` | Did bots find the player and fight, or did the player have to hunt for them? | Text |

## 9. Events, observations and counterexamples

- **Events** are countable facts with a time, a player and, where possible, an entity or space id. Prefer `demo_review` as the source; observer notes are a fallback and are labelled.
- **Observations** follow MAP_STUDY.md: attach entity or space ids and the evidence, and keep the **observation**, the **interpretation** and any **proposed rule** in separate fields. Name spaces by function ("lower west corridor"), using the ids in the map's report where they exist.
- **Counterexamples.** Every proposed rule has at least one counterexample or trade-off, citing observation ids. A failed run, a map that confused players, or a reviewer who disagreed is kept in the record.

## 10. Anchored ratings

Ratings use the MAP_STUDY.md anchors. A rating is given only after the relevant behaviour was observed, and it cites at least one observation id in `ratings[].basis`. The schema rejects a non-null score without a basis.

| Rating | Meaning |
| --- | --- |
| 1 | The intended activity repeatedly fails or becomes unusable |
| 2 | Repeated problems obstruct play; a specific cause is documented |
| 3 | Usable for the stated intent and load, with documented tradeoffs |
| 4 | Deliberate choices work clearly and survive matched comparisons |
| 5 | Strong repeated evidence across the intended conditions and independent reviewers |
| null | Not evaluated or evidence is insufficient |

Dimensions: the seven from the study template (`navigation`, `spaces`, `verticality`, `items`, `sightlines`, `theme`, `fun`) plus `spawns`, `encounters`, `progression` and `fairness`. Each rater gives one rating set per condition. A single session cannot support a 5, which needs independent reviewers across the intended conditions.

## 11. Null when not observed

- A value that was not observed is **null**. It is never 0, false, an average or a guess.
- 0 and false are observed values. `sp.secrets_found: 0` means the player finished the run and found none; if the run ended early, the answer is null.
- A rating dimension that the session did not exercise is null, for example `progression` in deathmatch.
- Nothing is inferred from silence: a player who did not mention getting lost has `all.lost: null` unless the question was asked.
- A whole condition that could not be played stays in the record with null results and a deviation saying why.

## 12. Blinded comparisons

When two variants of a map are compared (for example, one change on and off), follow MAP_STUDY.md:

- Label the variants neutrally (`variant_label`), and play them in an order randomised before the session.
- Use the same rules, settings and player load for both.
- Record wins, ties, disagreements and reasons. Do not treat every stock map as a positive or every generated map as a negative.
- Bind each variant's exact BSP hash. A comparison with a rebuilt map is a new comparison.

## 13. The session record

One JSON file per session at `out/playtest/<session_id>/session.json`, valid against [`tools/benchmark/schemas/playtest-session.schema.json`](../tools/benchmark/schemas/playtest-session.schema.json) (JSON Schema 2020-12). Session ids have the form `pt-YYYYMMDD-NN`.

Rules the schema enforces:

- `status: "planned"` is a run sheet: `evidence_level` is `"none"`, and it holds no answers, events, landmark checks or ratings.
- `completed`, `partial` and `aborted` sessions carry `evidence_level: "human play"`, the date, the user's approval, the protocol hash, the exact BSP hash, the engine and game-module names and hashes, the load command, at least one condition, and recorded consent for every participant who has not withdrawn.
- A generated map carries its `.map`, report and prompt hashes and its seed.
- A non-null rating cites at least one observation.
- Only a `completed` or `partial` session can be marked as entered in the ledger, and only with an entry id, a date and the user's approval.
- Demo, screenshot and settings paths lie under `out/playtest/`.

JSON Schema cannot express cross-references, so a session validator must also check that:

- every participant, condition, observation and landmark point id that is referenced is declared in the record;
- every answer's question id belongs to a question set of the record's path (section 8);
- a withdrawn participant has no roles, consent values, answers, events, landmark checks, observations or ratings left in the record;
- `participants[].consent.recorded_by` names a participant with the lead role.

No such validator exists yet. Until it does, the lead makes these checks by hand before a record is entered in the ledger.

**From the study template.** The reference-map template (`out/map-study/study-plan.template.json`) maps onto the record as follows.

| Template field | Session record field |
| --- | --- |
| `map_id`, `load_command` | `map.load_name`, `runtime.load_command` |
| `bsp_sha256`, `entity_variant_sha256` | `map.bsp_sha256`, `map.entity_variant_sha256` |
| `engine_sha256`, `game_module_sha256` | `runtime.engine.sha256`, `runtime.game_module.sha256` |
| `dmflags` | `runtime.dmflags` |
| `observed_players` | `conditions[].player_count` |
| `demo_path`, `engine_screenshot_paths` | `recordings.demos[]`, `recordings.screenshots[]` |
| `question` | `answers[]` under the mode question ids; a lesson question goes in `notes` |
| `observations`, `counterexamples` | `observations[]`, `counterexamples[]` |
| `reviewer`, `scores` | `participants[]` with the reviewer role, `ratings[]` (same seven keys plus four) |
| `rule_ids` | `rule_ids` |
| `status` | `status` |
| `lesson`, `partition`, `package_path` | Reference-study fields; not used for generated maps |

**A planned record.** A run sheet for a generated Quake II deathmatch map, written before anyone plays. Values not yet known are null. The all-zero hashes and seed 0 stand in for the real files' hashes and seed. This example validates against the schema.

```json
{
  "schema": "qmg.playtest_session",
  "schema_version": 1,
  "session_id": "pt-20261101-01",
  "protocol": {"path": "docs/PLAYTEST_PROTOCOL.md", "sha256": null},
  "status": "planned",
  "evidence_level": "none",
  "date": null,
  "user_approval": {"given": null, "date": null},
  "participants": [
    {"id": "P1", "roles": ["lead", "recorder"], "experience": null,
     "consent": {"given": null, "date": null, "recorded_by": null,
                 "demos": null, "screenshots": null, "notes": null},
     "withdrawn": false},
    {"id": "P2", "roles": ["player"], "experience": null,
     "consent": {"given": null, "date": null, "recorded_by": null,
                 "demos": null, "screenshots": null, "notes": null},
     "withdrawn": false}
  ],
  "map": {
    "game": "quake2",
    "path": "q2-dm",
    "source": "generated",
    "resolved_mode": "deathmatch",
    "module": null,
    "load_name": "qmg_example",
    "map_sha256": "0000000000000000000000000000000000000000000000000000000000000000",
    "bsp_sha256": "0000000000000000000000000000000000000000000000000000000000000000",
    "report_sha256": "0000000000000000000000000000000000000000000000000000000000000000",
    "aas_sha256": null,
    "arena_sha256": null,
    "entity_variant_sha256": null,
    "prompt_sha256": "0000000000000000000000000000000000000000000000000000000000000000",
    "seed": 0,
    "bench_case_id": null,
    "hero_id": null
  },
  "build": {"qmg_sha256": null, "generator_version": null, "compiler": []},
  "runtime": {
    "engine": {"name": null, "version": null, "sha256": null},
    "game_module": {"name": null, "sha256": null},
    "windowed": true,
    "network": "lan",
    "load_command": null,
    "settings": {"deathmatch": 1, "coop": 0, "fraglimit": null, "timelimit": 15},
    "settings_capture": null,
    "dmflags": null,
    "skill": null,
    "mode_check": null,
    "bots": []
  },
  "conditions": [
    {"id": "C1", "description": "Lower end of the prompt's player range", "player_count": 2,
     "team_sizes": null, "bot_count": 0, "skill": null, "starts_exchanged": null,
     "sides_swapped": null, "time_box_minutes": 15, "minutes_played": null, "variant_label": null},
    {"id": "C2", "description": "Upper end of the prompt's player range", "player_count": 8,
     "team_sizes": null, "bot_count": 0, "skill": null, "starts_exchanged": null,
     "sides_swapped": null, "time_box_minutes": 15, "minutes_played": null, "variant_label": null}
  ],
  "landmark_points": [
    {"point_id": "L1", "intended": "the room with the raised bridge", "entity_ref": null, "space_ref": null}
  ],
  "recordings": {"demos": [], "screenshots": []},
  "answers": [],
  "events": [],
  "landmark_checks": [],
  "observations": [],
  "counterexamples": [],
  "ratings": [],
  "rule_ids": [],
  "deviations": [],
  "ledger": {"entered": false, "entry_id": null, "entered_on": null, "user_approved": null},
  "notes": null
}
```

## 14. How results enter the ledger

1. The session record validates against the schema. A record that does not validate is not evidence.
2. The user approves the entry. The release-candidate ledger (workstream X1) assigns an entry id, and the record's `ledger` field is filled in with that id, the date and the approval.
3. The final record is validated again, then it, each demo and each screenshot are hashed. The ledger entry names the session id, the record's sha256, the path, the exact BSP hash, the evidence level `human play` and the status.
4. A human-play entry sits beside results at other evidence levels. It never changes or upgrades a static, compiler, native or engine result, and a failed session stays in the ledger as a failure.
5. A result applies only to the BSP hash it names. A rebuilt or regenerated map needs its own session.
6. Observations may be cited on knowledge cards as observations or counterexamples. No card's status is promoted on human evidence in rc1 (plan section 13).
7. README.md and docs/ROADMAP.md keep the sentence "No human playtest of any generated map has been done" until at least one entered session exists. The replacement wording names the sessions, maps and paths it covers, and the user approves it.

Until the ledger exists, records stay under `out/playtest/` and nothing is claimed from them.

## 15. What a session does not show

- A small number of players and sessions shows what those players did on that map, at that load and under those settings. It is not a general verdict on the map or the generator.
- Player skill and familiarity change results. Record experience levels and keep them in the comparison.
- A good rating on a generated map does not make its design rules true for other maps, games or modes.
- Demo-based counts depend on what the engine records. A count from notes is weaker evidence and is labelled.
- Bot behaviour in a bot-arena session is engine runtime with bots; only the human player's answers are human play.
