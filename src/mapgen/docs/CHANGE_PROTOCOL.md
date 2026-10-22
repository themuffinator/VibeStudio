# Change protocol

This is the procedure every change follows from Phase 2 on. It restates plan section 8.3 (change
protocol, sub-lanes, determinism) and decision D7 of `docs/RC_PHASE1_AUDIT_AND_PLAN.md`, and names the
tools that check each step. The tools live in `tools/benchmark/`; their gate policy is in
`tools/benchmark/README.md`.

## Steps

Every change follows these steps.

1. **Build and test.** `cmake --build --preset windows-clang` and `ctest --preset windows-clang` pass. The
   record keeps the command, the exit code and the pass/total/failed counts.
2. **Compiler fixtures.** A serialization or shell-construction change also passes the real Q1/Q2/Q3
   compiler fixtures.
3. **One cause, behind a switch.** The change has one cause. It lands behind a stage-profile switch that is
   off by default (D7), and with every switch at its default the map and report bytes are identical to the
   previous baseline. The switch interface is fixed: `qmg generate ... --switch <name>=on|off`
   (repeatable; dotted lowercase names; an unknown name is an error; a report gains a top-level `switches`
   object only when a switch is not at its default). The runner passes it through
   `run.py --qmg-extra "--switch <name>=on"`.
4. **Measure on and off, diff within the declared scope.** The affected fixed sets are run with the switch on
   and off. The off run must be byte-identical to the baseline. Then
   `python -B tools/benchmark/compare.py <off-run> <on-run> --scope <declared>` diffs report fields, sorted
   brush planes, the entity multiset, the texture histogram and the raw bytes. It must show 0 differences
   outside the declared scope; any out-of-scope difference fails the change (exit 1). The scope is a list of
   case ids written into the experiment record **before** the run. A Q2 DM change also reruns MP-PORTFOLIO
   and SPAN-PORTFOLIO (`run.py --manifest tools/benchmark/portfolios/mp-portfolio.json`, and the span
   manifest).
5. **Experiment record.** `knowledge/<game>/experiments/<id>.json`, from
   `tools/benchmark/templates/experiment-record.json`: hypothesis, expected mechanism, declared scope, before
   and after values, the scope diff, the verdict and the evidence level (D34 names).
6. **Flip separately, re-record the baseline.** The default flips in a separate commit, and the baseline is
   re-recorded as baseline-N in `tools/benchmark/baselines/index.json` (summary hashes only; outputs stay
   local under `out/`). `python -B tools/benchmark/ledger_check.py` validates the index.
7. **Keep everything.** Failed variants, counterexamples and earlier cards and experiments are kept. Nothing is
   deleted.

A failed cause keeps its record. It is then split, or redeclared with a wider scope under a new experiment
record.

## Output sub-lanes

Changes that alter generator output are made one at a time per fixed set, in two sub-lanes with disjoint
scopes:

- **MP sub-lane:** every DM, duel, tourney, TDM, CTF and bot-arena case.
- **SP sub-lane:** the Q1/Q2 SP and co-op cases, the SP CTest fixtures and the unit prompts.

Each sub-lane has at most one output change in flight. A change declares its sub-lane, and
`compare.py --scope` must show 0 byte differences on every case of the other sub-lane, or the change is
rejected. A change that touches both sub-lanes (for example a prompt-normalization change) waits until neither
sub-lane has a change in flight. Harness, reader and docs work does not touch generator output and runs in
parallel.

In BENCH-v1 the `path` field decides the sub-lane: `*-sp` and `*-coop` paths are SP; every other path is MP.

## Determinism

- Every stage is a pure function of the canonical normalized brief, the plan, the candidate seed, the stage
  revision, and the profile and registry sha256.
- Each stage draws from its own stream (splitmix64 of candidate_seed XOR a stage tag), so inserting a stage
  never shifts earlier draws.
- Only the project's own `uniform_integer` is used, never std distributions.
- The GGUF model revision and `n_threads` are part of the key.
- Repair re-runs the stage chain, not the legacy `EntityAllocator`.

G8 (`run.py --repeat 2`) checks byte-identical `.map` and `.qmg.json` across two processes. The D5
determinism-key change lands last in rank 1, as its own cause with its own baseline.

## Records until D1 is decided

The commit and baseline decision (D1) is pending, so nothing is committed, branched, tagged, stashed or reset.
Until it is decided, every change record lives under `out/phase2/changes/<change-id>/`:

- `before/<repo-relative path>`: a copy of each existing file taken before its first edit;
- `change.patch`: a unified diff of the `before/` copies against the current files, plus the new files;
- `record.json`: `{id, title, cause, kind ("infra" | "output"), files, build {cmd, exit},
  ctest {passed, total, failed}, measurements, declared_scope, scope_diff, result ("pass" | "fail" |
  "partial"), evidence_level, notes}`.

The working tree's uncommitted user changes are never reverted, discarded, reformatted or rewritten. Each
change edits only the files it declares. When D1 is decided, these records become the commit messages and
experiment records of the grouped commits.

## Baselines

`tools/benchmark/baselines/index.json` lists every recorded baseline: id, qmg sha256, runner sha256 (the
sha256 of `run.py`), manifest and manifest sha256, policy, out directory, summary sha256, source snapshot,
cause, date and notes. Baseline-0 is the Phase-1 build (qmg `815bb58d…`) run on BENCH-v1; it must reproduce the
draft-v0 counts under `--policy draft` (G1 138, G2 70, G3 138, G4 138, G5 137, G6 40/40, G7 85/29/24,
all-gates 70). Each baseline-N records the flip of exactly one cause.

## Switches

Every generator-output change lands behind a named boolean stage switch that is off by default (D7). The
mechanism was introduced by change `d7-switches`.

### Interface

- **CLI.** `qmg generate ... --switch <name>=on|off`, repeatable. The value is exactly `on` or `off`. An unknown
  name, a name given twice (even with the same value) and a malformed value are errors: qmg exits 1 and writes
  nothing. Setting a switch to its default is allowed and is the same run as not setting it.
- **Runner.** `python -B tools/benchmark/run.py ... --qmg-extra "--switch <name>=on"`.
- **Default bytes.** With every switch at its default, the `.map` and `.qmg.json` bytes, the layout fingerprint and
  the `generate` console output are unchanged. `qmg_switch_cli` checks this for `--switch selftest.noop=off`; change `d7-switches`
  measured it on every non-sealed BENCH-v1 case and every portfolio case.
- **Report.** When any switch is away from its default, the report gains one top-level member after
  `generator_version`, listing only the non-default switches in name order, for example
  `"switches": {"r1s.q1_intermission": "on"}`. `schema_version` stays 4: report v5 lands in Phase 3 (D2), and this
  member is absent from every default report. qmg also prints a `switches=<name>=on,...` line.
- **Fingerprint.** A layout generated with a non-default switch hashes in its own domain: `canonical_layout_sha256`
  leads with `qmg.layout.switches.v1`, the count and each non-default (name, value) in name order, then the
  unchanged historical encoding. Every `layout_sha256` in the report (top level and `analysis.*`) therefore
  changes on a switched run, and a switched layout can never share a fingerprint with an unswitched one.
- **Stages.** `PipelineOptions::switches` is copied into the parsed `PromptSpec`, so every stage reads it from the
  spec or layout it already receives: `layout.spec.switches.on(qmg::StageSwitch::<id>)`. The prompt text never
  sets a switch.

### The registry

The registry is `kRegistry` in `src/switches.cpp`; the typed handles are `enum class StageSwitch` in
`include/qmg/switches.hpp`. Each row holds the name, `default_on`, the sub-lane (`none`, `mp`, `sp`, `both`), the
single cause, and the change id that introduced it. Compile-time checks require the rows to be sorted by name and
unique, every name to be valid, and each enumerator to equal its row index. A switch set stores one bit per
switch, so at most 64 switches are live at once; retiring switches keeps the count down. Enumerator values are
never serialized (reports and fingerprints carry names), so a new row may be inserted anywhere in name order.

### Naming

- A name is `<workstream>.<cause>`: two or more dot-separated segments, each `[a-z][a-z0-9_]*`, at most 64 bytes.
  The first segment is the plan item, lowercased without punctuation (`r1s`, `r1a` ... `r1h`, `da`, `f1`, `g1`,
  `c1`, `e1`). The rest names the one cause in snake case, with a game prefix when only one game changes, for
  example `r1s.q1_intermission`.
- `selftest.*` is reserved for mechanism self-tests that change no generator output. They are never flipped or
  retired while the mechanism exists.
- A name is never reused, even after its switch is retired, so old reports and experiment records stay
  unambiguous.

### Adding a switch

This is part of the change that introduces the cause, and the switch lands off.

1. Add the `StageSwitch` enumerator and its `kRegistry` row at the same index, in name order: name,
   `default_on = false`, sub-lane, cause, and change id.
2. Guard the new behaviour in the stage with `spec.switches.on(StageSwitch::<id>)`. The off path must be the old
   code path exactly: no extra or reordered random draws, no changed iteration order, no changed arithmetic.
3. Add unit tests for the on path. Add a row to the table below.
4. Build and run ctest. Then prove default bytes: `run.py --generate-only` on every affected fixed set with the
   previous qmg and the new qmg, with no `--switch`, and
   `compare.py <previous> <new> --generate-only` with no `--scope` must show 0 differences.
5. Measure on and off as in step 4 of the steps above (`--qmg-extra "--switch <name>=on"`), and write the
   experiment record with the declared scope.

**Known gap (compare.py `a738936…`).** An on-run report always differs from the off-run in `$.switches` and in
every `layout_sha256` field. These are identity fields of the switched run, not output changes, but compare.py
counts them, so every case, in scope or not, differs. Change `d7-switches` measured this with the no-op
`selftest.noop` switch: 459 of 459 cases out of scope in compare.py, while 459 of 459 maps were byte-identical
and 459 of 459 reports were identical once that member and the fingerprints were removed. compare.py has to
discount exactly these fields on switched runs before the first cause (R1-s) is measured. Every other report
field, and the map categories, stay strict.

### Flipping a switch

1. Flip in a separate change (a separate commit once D1 is decided), one switch per change: set `default_on = true`
   in the row. Nothing else changes.
2. Check the flip. The flipped build at its defaults must reproduce the pre-flip on-run: identical `.map` bytes,
   and reports identical apart from the `switches` member and the fingerprints. `--switch <name>=off` on the
   flipped build must reproduce the pre-flip baseline's `.map` bytes.
3. Re-record the baseline as baseline-N in `tools/benchmark/baselines/index.json`, with `cause` set to the flip's
   change id. Fill in "flipped by" and "baseline before / after" in the table, and `default_flip.new_baseline_id`
   in the experiment record.

### Retiring a switch

- **After a flip.** Retire the switch once at least one baseline has been recorded after its flip and no open
  experiment needs its off path. Retire it in its own change: remove the off path so the behaviour is
  unconditional, then remove the enumerator and the registry row. Retirement is a byte-equivalent refactor: the
  default output of the retiring build must equal the flipped baseline (`compare.py`, no `--scope`, 0
  differences). Afterwards `--switch <name>=...` is an unknown-name error.
- **A rejected cause.** Remove its on path and its row without flipping. Default bytes are unchanged by
  construction, and the failed experiment record is kept.
- **In both cases** the table row stays, with "retired by" filled in.

### Switch table

| switch | cause | sub-lane | default | introduced by | flipped by | baseline before / after | retired by |
|---|---|---|---|---|---|---|---|
| `r1s.monster_hull_support` | R1-s entity safety (5), audit gap 6: every monster of a Quake 1 or Quake 2 single-player map is tested with its class's real spawn box (`monster_hulls` in the Quake 1 and Quake 2 `data/profiles/*/generator-current.json`, cited to the id source); a monster whose box meets a solid brush (or touches one), hangs over a support edge or touches a wall moves, keeping its class, facing and id, to the nearest station of its room on a 16-unit lattice through its slot where the box stands strictly inside one solid top (or on the floor), falls from spawn height clear of every solid and keeps 16 units from every other entity; with no such station it is left out. With the switch on the validator reports an unsafe box as `placement_in_solid`, so repair re-runs the allocator | sp | on | `r1s-monster-hull-support` | `r1s-monster-hull-support-flip` | `baseline-3` / `baseline-4` | — |
| `r1s.mp_player_start` | R1-s entity safety (2): every Quake 1 and Quake 2 deathmatch, duel and CTF map gets exactly one `info_player_start` (existing role `player_start`), appended last at a supported, clear, lit standing station of its start room, so `map <name>` with deathmatch 0 does not abort; deathmatch spawn selection never reads it, so match analyses leave it out | mp | on | `r1s-mp-player-start` | `r1s-mp-player-start-flip` | `baseline-1` / `baseline-2` | — |
| `r1s.q1_intermission` | R1-s entity safety (1): every Quake 1 map gets one `info_intermission` camera (D8 role `intermission`) at a standing-eye viewpoint in its largest room, with a `mangle` facing the room centre, so FindIntermission never reaches objerror at a level or match end | both | on | `r1s-q1-intermission` | `r1s-q1-intermission-flip` | `baseline-0` / `baseline-1` | — |
| `r1s.q3_ctf_team_begin` | R1-s entity safety (4): every Quake 3 CTF map gets ceil(target_players / 2) `team_CTF_redplayer` and as many `team_CTF_blueplayer` team-begin spots (existing role `team_initial_spawn`), the team's existing spot plus new ones appended last, red first, at the supported, clear, lit standing stations of the team's base room (the room of its flag) nearest that spot, at least 64 units from each other and from every other entity, facing the base centre, so a team that begins together does not telefrag on one spot; the `team_CTF_*spawn` respawn spots are kept; with the switch on the validator's `ctf_spawn_contract` counts the spots per team | mp | off | `r1s-q3-ctf-team-begin` | — (flip attempt `r1s-q3-ctf-team-begin-flip` failed and was reverted: the on path fails closed on 2 of 16 seeds of the 2-room, 32-player dense fixture of `qmg_tests`; the switch stays off until a revised variant passes) | `baseline-3` / — | — |
| `r1s.sp_coop_starts` | R1-s entity safety (3): every Quake 1 and Quake 2 single-player map gets three `info_player_coop` (D8 role `coop_start`), appended last at the supported, clear, lit standing stations of its start room nearest the `info_player_start`, at least 64 units from each other and from every other entity, facing the start's way, with no targetname, so co-op players 2 to 4 do not telefrag on the one start; single-player play never reads them, so the single-player match analyses leave them out | sp | on | `r1s-sp-coop-starts` | `r1s-sp-coop-starts-flip` | `baseline-2` / `baseline-3` | — |
| `selftest.noop` | D7 mechanism self-test: changes no generator behaviour; exercises the CLI, the stage query, the report `switches` member and the layout fingerprint | none | off | `d7-switches` | never (self-test) | — | — |
