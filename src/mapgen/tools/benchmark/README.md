# Benchmark: fixed sets, runner v1 and gate policy

This directory holds the measuring stick of plan section 8.3 and workstream X1 (section 9.1 of
`docs/RC_PHASE1_AUDIT_AND_PLAN.md`): the frozen fixed sets, the runner, the scoped comparison tool, the
gate controls, the release-candidate ledger and its checkers. Everything is Python 3, standard library
only. Run every script with `python -B`.

Outputs stay local under `out/` (git-ignored). The runner never passes `--game` or `--mode`, never
launches a game client or engine, never creates links inside game directories, and only reads game
assets.

## Files

| file | what it is |
|---|---|
| `rc1-v1.json` | **BENCH-v1**, frozen. The 138 draft-v0 cases verbatim plus 273 new cases (411). |
| `contra-v1.json` | **CONTRA-v1**, frozen. The 34 contradictory ids, as an id set over BENCH-v1. |
| `parse-matrix-v1.json` | **PARSE-MATRIX-v1**, frozen. A data table for the rank-1 C++ normalization test. |
| `portfolios/*.json` | **MP-PORTFOLIO** (11), **SPAN-PORTFOLIO** (14), **LATTICE-PORTFOLIO** (15), **SPLICE-PORTFOLIO** (9), frozen. |
| `sealed.json` | The seal list: q2-showcase-hero (D26). |
| `heroes.json` | **HEROES**: entry schema and an empty list. No hero seed is chosen yet. |
| `manifests.json` | sha256 registry of every fixed-set file. |
| `draft-v0.json` | The Phase-1 draft manifest (sha256 `056889a0…`), unchanged. |
| `freeze_sets.py` | The recorded procedure that produced the fixed sets. `--check` rebuilds them in memory and compares; `--write` writes only files that do not exist. |
| `run.py` | Runner v1. `--policy draft` keeps the draft-v0 gate behaviour. |
| `compare.py` | Scoped diff of two run directories, with `--self-test`. |
| `rc-ledger.json`, `rc-ledger.schema.json` | The RC ledger (17 paths) and its schema. |
| `parity.py` | Parity rubric (P1–P11) report; `--strict` fails a claimed path that lacks rubric evidence. |
| `ledger_check.py` | Ledger and registry integrity. |
| `baselines/index.json` | The tracked baselines index (hashes only). |
| `decisions.json` | Measured records behind gate-policy decisions: D11(3), the light budget (frozen at 1020 s). |
| `templates/experiment-record.json` | The experiment-record template (change protocol step 5). |

## Fixed sets

The fixed sets are frozen once (plan 8.3). Cases are never edited; new cases get new ids in a new set
version, and the registry gets a new row. `run.py` refuses a registered frozen manifest whose bytes differ
from `manifests.json` (override only with `--allow-modified-manifest`, which is recorded in the run identity).
A file cannot carry its own byte hash, so `rc1-v1.json` records `cases_sha256` (the canonical-JSON hash of
its `cases`) in its header and the file's byte sha256 is in `manifests.json`.

**BENCH-v1 case fields.** The draft fields (`id, target, game, mode, category, prompt, seed,
expected_requests, notes`) are kept verbatim for the 138 draft cases. Every case adds:

- `path`: one of the 17 paths (`q1-sp q1-coop q1-dm q1-duel q1-tdm q1-ctf-threewave q2-sp q2-coop q2-dm
  q2-duel q2-tdm q2-ctf q3-botarena q3-ffa q3-tourney q3-tdm q3-ctf`). The two showcase cases use `q2-dm`.
- `claimed`: the plan section 12 tier, `committed | conditional | deferred | dropped-claim`.
- `module`: the expected game module, `q1-id1 | q1-threewave-5.0 | q2-baseq2-3.19 | q2-ctf-3.19 |
  q3-baseq3-1.32b`. `q1-id1` does not choose between the 1996 progs and the re-release (D13 is pending).
- `expected_conflicts`: `[{class, span}]`, where `span` is a list of `{start, end, text}` UTF-8 byte spans
  into the prompt. The classes are a closed list in the manifest header (`conflict_classes`).
- `expected_resolution`: `null`, or `{action, primary {game, mode, module}, rules, unmet}`. `rules` name
  entries of the header's `resolution_rules`; `unmet` lists the expected requests the resolution leaves
  unmet, each of which needs a typed finding (G9). No case is predeclared unservable, so `action` is always
  `generate` (D11(2)).

New cases carry `added_in: "BENCH-v1"` and seeds `2000 + index`. They are: 14 parse regressions (number
words, `2-8`, `2 to 8`, `2–8` with U+2013, `for 16 people`, `two players`, map-name references, the
negations `no loops`, `not vertical`, `no water`, and the false positives `database`, `two bases`,
`open space`); 24 item cases (8 per game); 192 mechanism cases (8 per mechanism per game: Q1 and Q2 lift,
door, teleporter, water, slime, lava, ramp, drop; Q3 jump pad, launch pad, teleporter, lift, water, lava,
ramp, drop; field `mechanism`); 4 Q1 CTF prompts that name Threewave (more Threewave prompts appear among
the item and mechanism cases); 6 size, 6 mood, 9 forbidden-item and 6 landmark cases (at least 2 of each per
game, 3 forbidden-item cases per game); and 12 SP cases with keys, secrets and objectives (6 per Q1/Q2).

**Portfolios.** Each entry carries the exact prompt bytes (`prompt`, with its original line ending), the
prompt sha256, the recorded map name and `qmg_args`
(`generate --prompt-file {prompt_file} --output <map_name>.map`, all other options at their defaults; the
request seed is in the prompt text). The invocation is taken from the retained records
(`out/mp-portfolio/final-cli-parity/parity.json`, `out/span-portfolio/protocol.json`,
`out/lattice-portfolio/generate_final.py`; Splice has no retained script, which its manifest records as a gap).
Every recorded report's `prompt` field equals the prompt file. Regenerating with the baseline-0 binary
(`815bb58d…`, generate-only, 2026-10-15) reproduced all 14 SPAN-PORTFOLIO outputs byte for byte; the MP,
Lattice and Splice records predate later generator changes and differ (Lattice keeps all 15 selected
candidate seeds but not the geometry; MP and Splice differ in selection). Their regression baseline is
therefore the baseline-0 regeneration, not the recorded files.

## Commands

```sh
# gate controls (project-authored fixtures under out/benchmark-v1/controls/)
python -B tools/benchmark/run.py --controls
# a BENCH-v1 run (v1 policy, resumable; cases with result.json are skipped unless --force)
python -B tools/benchmark/run.py --out out/benchmark-v1/run --jobs 8
python -B tools/benchmark/run.py --filter q2-dm --repeat 2
# generate-only (G1, G2, G7-G12 from the generated files; compile gates "not run")
python -B tools/benchmark/run.py --generate-only --filter q3- --out out/benchmark-v1/gen
# switch on, measured against switch off (the switch interface is fixed: --switch <name>=on|off)
python -B tools/benchmark/run.py --qmg-extra "--switch r1s.q1_intermission=on" --out out/...
# re-evaluate an earlier run without recompiling (its qmg sha256 must match --qmg)
python -B tools/benchmark/run.py --import out/benchmark-draft --policy draft --out out/benchmark-v1/import
# CONTRA-v1 or a portfolio
python -B tools/benchmark/run.py --manifest tools/benchmark/contra-v1.json --out out/...
python -B tools/benchmark/run.py --manifest tools/benchmark/portfolios/mp-portfolio.json --out out/...
python -B tools/benchmark/run.py --summary-only --out out/benchmark-v1/run
# scoped comparison, ledger and parity
python -B tools/benchmark/compare.py out/A out/B --scope @scope.txt --generate-only
python -B tools/benchmark/compare.py --self-test
python -B tools/benchmark/ledger_check.py
python -B tools/benchmark/parity.py --path q2-dm --path q2-duel
python -B tools/benchmark/freeze_sets.py
```

Main options: `--manifest`, `--out`, `--filter` (substring, repeatable), `--jobs` (default 8), `--force`,
`--generate-only`, `--qmg-extra "<args>"`, `--repeat 2`, `--import <run-dir>`, `--summary-only`,
`--controls`, `--light-budget <s>` (default 1200, the superseded provisional value; the frozen budget is
1020, see Light budget), `--light-timeout <s>` (default 2 × budget, v1 policy only), `--policy v1|draft`,
`--hero-run`, `--q1-wad <path>`, `--qmg <path>` (default `build/windows-clang/qmg.exe`), `--allow-unpinned`.

## Runner identity

`RUNNER_VERSION` is the sha256 of `run.py`. Every `result.json` carries an `identity` block: runner
sha256, manifest path and sha256 (and whether it is registered), qmg.exe path and sha256, `--qmg-extra`,
the source snapshot (git HEAD, sha256 of `git diff --no-color --no-ext-diff`, sha256 of the sorted
`<sha256>  <path>` list of untracked files; read-only git with `--no-optional-locks`), the tool sha256
values and pins, the seal-list sha256, the policy, the light budget and timeout, and the concurrency. Each
invocation also writes `run.json` and appends to `runs.jsonl`. The summary **refuses** (exit 3) a run
directory whose results carry different runner hashes, different qmg hashes or different policies; use a
fresh `--out` or `--force`. Tool pins (ericw-tools 2.0.0-alpha11; the configured q3map2 and mbspc) are
checked before compiling; a mismatch stops the run unless `--allow-unpinned`.

**Seal.** A case listed in `sealed.json`, or any case whose normalized prompt has the same sha256, is
refused unless `--hero-run` is given. Each refusal is printed and appended to `<out>/seal-refusals.jsonl`.
`--import` re-evaluates a sealed case read-only and does not regenerate it.

**Import.** `--import <run-dir>` requires the earlier run's recorded qmg sha256 (its `run.json` or
`summary.json`) to equal `--qmg`. For each case it checks that the recorded command equals the manifest's
invocation, regenerates `.map` and `.qmg.json` in a fresh process, requires byte identity with the imported
files (this is also the case's G8 measurement), rebuilds the compile record from the imported stage logs and
files, re-evaluates the gates and records gate and request-status differences against the imported
`result.json`. The intermediate BSP inspections are read from the final BSP (light does not change the
header or the visdata), and are marked `inferred_from_final_bsp`.

## Gate policy

G1–G7 are **hard** from now, with the v1 fixes to G4 and G5. G8–G15 are **reported** columns; each becomes
hard when the workstream that owns it passes its exit gate, so baseline-0 stays comparable and later passes
are not loosened. `all_hard_gates` counts G1–G7 only; `all_hard_gates_with_G10` is reported beside it.
Statuses: `pass`, `fail`, `n/a`, `not run` (generate-only, or determinism not requested), `not configured`.

| gate | checks (v1 policy) | owner | hard from |
|---|---|---|---|
| G1 generated | qmg exits 0, the `.map` is not empty, the report parses. A contradictory case must still generate unless its manifest entry predeclares a refusal (D11). | X1 | now |
| G2 intent | resolved game and primary mode equal the path's; the module and the player range are checked when the report carries them (report v5). With schema-v4 reports this gives exactly the draft verdicts: co-op, TDM and bot arena have no generator mode, and Threewave needs a reported `q1-threewave-5.0` module. | rank 1 | now (mapping extended) |
| G3 leak-free compile | BSP stage exits 0, no `.pts`/`.lin` and no leak message, no invalid-brush diagnostics. | X1 | now |
| G4 full VIS | the draft checks plus: an anchored `Calculating Full Vis:` line, no `"fast" was set to "1"` option line and `c_chains` > 0 (ericw vis); no `fastvis = true` line and an anchored `--- PassagePortalFlow (N) ---` or `--- PortalFlow (N) ---` line (q3map2); no `-fast` on the recorded command line. | X1 | Phase 2 |
| G5 final light | Q1/Q2: exactly one anchored line `^\s*0 empty lightmaps\s*$`; Q3: a final anchored `N total lightmaps` line with N > 0; a nonempty light lump; the light wall time within the light budget. | X1 | Phase 2 |
| G6 Q3 AAS | EAAS magic, version 5, areas == settings, reachability totals consistent (BSPC global consistency). | X1 | now |
| G7 mode entities | as draft-v0, read from the `.map` entity list. | rank 1 | strict contract from the Phase-3 exit |
| G8 determinism | byte-identical `.map` and `.qmg.json` across two processes (`--repeat 2`, or the `--import` regeneration). | X1 | Phase-2 exit |
| G9 request accounting | every expected request (except `conflict surfaced`, which is G10) is honoured, or covered by a typed report finding that names it (`findings[].request`, `unmet[].request`, or a `request_trace` entry with a finding). Today's schema-v4 reports have no typed findings, so every unhonoured request is a silent drop. | rank 2 | Phase-3 exit |
| G10 conflict surfaced | every `expected_conflicts` entry appears as a typed finding of its class (`class`, or `code` of `<class>`, `prompt.<class>` or `prompt.conflict.<class>`) whose spans overlap every expected span, and the report's game and mode equal the declared resolution's primary. `n/a` without expected conflicts. | rank 2 | Phase-3 exit |
| G11 error findings | error-severity analysis findings, counted by analyzer and code; pass when there are none. Waivers apply (below). | H5 | MP navigation/reachability errors: Phase-3 exit; `unlit_critical_placement`: each game's lighting exit (Q2 Phase 4, Q3 Phase 5, Q1 Phase 6); SP errors: Phase 7 |
| G12 materials | Q2: every texture resolves to a `.wal` under the Q2 assets, and qbsp (run without `-notex`) reports no `Couldn't locate texture` line for a map texture (compiler-internal lookups such as `skip` are listed, not failed: a project-authored map with no `skip` texture prints the same line). Q3: every shader or texture resolves to a shader script entry or an image. Q1: requires `--q1-wad`; without it the gate fails with "no WAD (D9 pending)". | F | Phase-3 exit (Q1 only with a WAD) |
| G13 lighting readability | not defined until Phase 3 (`n/a`). The floor-facing stored-luxel shares (`<8`, `<16`, `<32`, `>=250`, mean, median) are recorded per case with the logic of `out/benchmark-draft/probes/lighting-calibration/lm.py`. | G | each game's depth exit |
| G14 engine smoke | `not configured`. | H | per game, as its harness lands |
| G15 Q3 per-entity reachability | `not configured` (Q3); `n/a` otherwise. | H2 | Phase 5 |

Under `--policy draft`, G1–G7 are exactly the draft-v0 definitions (below) and the light stage keeps the
600 s timeout; G8–G15 are still computed and reported. Both G4/G5 variants are recorded in every result
(`gate_variants`).

**The `-fast` discriminator** was found by experiment on a project-authored three-room box (2026-10-15):
ericw `vis -fast` still prints `Calculating Full Vis:` but adds the option line
`"fast" was set to "1" (from command line)` and reports `c_chains: 0`; full VIS reported `c_chains: 98`
on the same map. `q3map2 -vis -fast` prints `fastvis = true` and skips the `CreatePassages` and
`PassagePortalFlow` passes. On the 98 Q1/Q2 draft-v0 VIS logs the minimum `c_chains` is 50, and every log has
the anchored `Calculating Full Vis:` line.

**Q3 liquids and sky** are detected by shader `surfaceparm` (`water`, `lava`, `slime`, `sky`) read from
`scripts/*.shader` (loose and inside `.pk3`), not by texture-name substring.

### Light budget

**Frozen at 1020 s** (2026-10-15, change x1-light-budget; D11(3) record in `decisions.json`): final light must
finish within **1020 s** at the fixed concurrency of **8 jobs × 2 threads** (`--jobs 8`; every tool runs with
2 threads). The provisional 1200 s, which replaced the 600 s cap that failed q2-tdm-c2, is superseded; the
freeze tightens it. In Phase 4 the budget is re-measured once with G2 fixtures and sun. Each change of the
budget is recorded in D11(3), and the gate is never loosened afterwards. The summary lists the 10 slowest
light stages and every case over budget; the light stage timeout under the v1 policy is 2 × the budget
(2040 s), so over-budget times are measured rather than cut off. `identity.concurrency.matches_light_budget_rule`
says whether a run used 8 jobs.

`run.py` still defaults to the provisional 1200 s (`DEFAULT_LIGHT_BUDGET`), and `rc_benchmark.py` uses that
default. Until the default is changed in its own change (it changes the runner sha256), pass
`--light-budget 1020` to every budget-gated run.

How it was measured (compiler output; baseline-0 binary `815bb58d…`, runner `d061a4b0…`, ericw-tools
2.0.0-alpha11 `light -extra4 -bounce 1 -soft -threads 2`). The 10 slowest BENCH-v1 cases by final-light wall
time in the baseline-0 v1-policy summary were recompiled at `--jobs 8` (their `.map` and `.qmg.json` are
byte-identical to baseline-0):

| case | rooms | recorded in baseline-0 s | run A s | run B s |
|---|---|---|---|---|
| q2-tdm-c2 | 32 | 600.1 (600 s cap) | 417.2 | **679.5** |
| q2-dm-s2 | 10 | 330.5 | 148.0 | 194.0 |
| q2-tdm-s1 | 10 | 289.4 | 146.0 | 190.6 |
| q2-tdm-c1 | 6 | 181.0 | 72.1 | 85.5 |
| q2-duel-c2 | 6 | 171.8 | 67.2 | 81.4 |
| q2-duel-s1 | 5 | 165.1 | 77.8 | 100.1 |
| q2-tdm-v1 | 6 | 126.2 | 50.6 | 62.1 |
| q2-duel-x-parse-2 | 6 | 99.8 | 62.7 | 70.0 |
| q2-ctf-s1 | 12 | 94.8 | 44.5 | 50.1 |
| q2-duel-s3 | 6 | 94.3 | 60.8 | 73.4 |

- **Run A** (`out/benchmark-v1/light-budget/top10-8x2`): the 10 cases alone. It did not hold the load: the
  other nine finished within about 150 s, so q2-tdm-c2 had all 8 jobs active for only 11.8% of its light
  stage and was the only job for 68.5%. 1.5 × 417.2 s would give 626 s, which q2-tdm-c2 exceeds under
  sustained load. Run A is kept as that counterexample and is not the basis of the freeze.
- **Run B** (`out/benchmark-v1/light-budget/sustained-8x2`): the same 10 plus every non-sealed BENCH-v1 case
  after q2-tdm-c2 in manifest order (320 filler cases, chosen by a fixed rule, not by inspecting maps), so
  all 8 job slots stayed busy for 100% of q2-tdm-c2's light stage. The slowest time is 679.491 s
  (q2-tdm-c2); 1.5 × 679.491 s = 1019.24 s, rounded up to the whole second like the provisional value:
  **1020 s**. All 330 cases passed G3–G5, and none lit for more than 1020 s.
- **Machine load, stated plainly.** i7-13700H laptop (20 logical CPUs) on AC power. The machine was not
  idle: other processes used about 3.8 CPUs during run B's slowest light stage (Windows Defender, the
  Claude app, other repositories' builds, tests and smoke tests) and about 6.3 CPUs during run A's. The
  machine was 99.7% busy during run B's slowest stage. No foreign compile tool ran.
- The recorded baseline-0 times of these inputs (9 from Phase 1) are 1.4–2.6× run A's, and the Phase-1 solo
  rerun of q2-tdm-c2 took 790 s. The load of those runs was not recorded, so the cause is unknown. A run
  under heavier load than run B can exceed 1020 s with no generator change. Record the machine load beside
  every budget-gated run (the sampler used here is local and untracked:
`out/phase2/changes/x1-light-budget/scripts/load_sampler.ps1`).
- Baseline-0 re-summarized under this budget: `out/benchmark-v1/baseline-0/v1-policy-frozen-budget`. G5 went
  from 409/410 to 410/410: q2-tdm-c2 was re-run in run A (417.2 s). Every other count is unchanged.

### Waivers

Wherever a gate or test allows a waiver (D11(8)):

- Each waiver has a reason code from the closed list below, the finding or instance id, and the user's
  approval recorded in `rc-ledger.json` (`approved_by_user: {by: "user", date, record}`). An agent cannot
  approve its own waivers.
- Waivers are capped at **5%** of the measured instances per gate per path. Above the cap the gate fails.
- Heroes allow **0** waivers, unless the hero's HEROES entry declared the exception (`declared_out_of_scope`)
  before generation.
- The waiver count and share are reported beside every pass rate that uses them (`ledger_check.py` prints
  them; the summary carries `waivers`).

### Waiver reason codes

The closed list (also the `reason_code` enum in `rc-ledger.schema.json`; `ledger_check.py` fails if they
differ):

- `stock_parity`: the same finding occurs at the same or a higher rate on the calibrated stock reference maps;
  the calibration aggregate is cited.
- `analyzer_false_positive`: a minimal project-authored reproduction shows the analyzer is wrong, and the
  analyzer fix is recorded as its own change.
- `tool_defect_upstream`: a compiler or tool defect outside the project, reproduced on a project-authored fixture.
- `runtime_verified_harmless`: engine evidence, at a named evidence level, shows no player-facing effect.
- `input_pending_user_decision`: blocked on a named pending decision (for example D9); expires when it is decided.
- `declared_out_of_scope`: heroes only; declared in the HEROES entry before generation.

**Hero honour rule (D11(5) (a)).** A hero passes when 100% of the requests not declared out of scope in its
HEROES entry before generation are honoured (15/15 for the Q2 DM hero, none declared).

## Gate controls

`run.py --controls` builds project-authored fixtures under `out/benchmark-v1/controls/` and proves every
gate it can test accepts its positive and rejects its negative (`controls.json`, `controls.md`; exit 1 if any
control is misclassified). Compiler-backed (real ericw-tools, q3map2 and BSPC runs): sealed box (G3+, G4+,
G5+, Q1/Q2/Q3), leaky box (G3−), `vis -fast` BSP (G4−; the draft G4 accepts it), light over a 0.001 s budget
(G5−), the real light log with its line changed to `10 empty lightmaps` (G5−; a substring check would accept
it), a valid AAS (G6+), a truncated and a version-altered AAS (G6−), a stock Q2 texture and a missing texture
compiled without `-notex` (G12+/−). Static: a stock and a missing Q3 shader (G12), no WAD and a project-authored
16×16 WAD (G12 Q1), a real qmg generation and an invalid-argument generation (G1), the generated report against
the right and the wrong path (G2), the generated map and a one-spawn map (G7), two processes and a one-byte
mutation (G8), report fixtures with and without a typed finding for an unsupported request (G9), with a
surfaced conflict, without one and with the wrong class (G10), and with and without an error finding (G11).
G13–G15 have no controls: G13 is not defined until Phase 3, G14 and G15 are not configured.

## Request vocabulary

The draft-v0 evaluator is unchanged under `--policy draft`. The v1 evaluator adds: `door` (`func_door`),
`ramp` (navigation slope nodes), `drop` (navigation drop edge of at least 64 u; `partial`, since a designed
drop route is not verified), `launch pad` (Q3 `trigger_push`; `partial`, launch and jump pads are not
distinguished), `objective` (Q2 `target_goal`/`target_help`), `no loop route` (acyclic room graph),
`no powerups` (per-game powerup classnames from the id sources), `style reference <map>`, `size <band>`,
`mood <word>`, `landmark <kind>` and `runes` (reported `unverified`: the report has no field for them), more
items (Q1 pentagram, ring of shadows, biosuit, green armor, thunderbolt; Q2 machinegun, BFG10K,
invulnerability, jacket armor, bandolier, ammo pack, adrenaline, silencer; Q3 machinegun, BFG, regeneration,
haste, battle suit, invisibility, flight, medkit, personal teleporter) and Q3 surfaceparm liquids and sky.
On the 138 draft-v0 cases the v1 evaluator gives the same statuses as the draft evaluator.

## Draft v0 (baseline-0)

`draft-v0.json` (17 targets × 8 prompts plus two `q2-showcase` probes at seed 4242) is the baseline-0
manifest; its cases are the first 138 BENCH-v1 cases. Draft gates, kept under `--policy draft`:

| gate | passes when |
|---|---|
| G1 generated | qmg exits 0, the `.map` is not empty, and the report parses |
| G2 intent_match | the resolved game and mode equal the target's game and mode (`sp` single_player; `dm`, `ffa` deathmatch; `duel`, `tourney` duel; `ctf` capture_the_flag; `coop`, `tdm`, `botarena`, `ctf-threewave` not representable) |
| G3 compile | the BSP stage exits 0, there is no leak (no `.pts`/`.lin` file and no leak message), and there are no invalid-brush diagnostics |
| G4 full VIS | the `.prt` file and the VIS visdata lump are not empty; Q1/Q2 logs contain "calculating full vis" |
| G5 final light | the lightdata lump is not empty |
| G6 Q3 AAS | the file has the EAAS magic and is version 5, `-aasinfo` gives areas == settings, and the reachability totals are consistent (Q1/Q2: n/a) |
| G7 mode entities | `sp`: `info_player_start` plus the game's exit (Q2: a `trigger_once` linked to a `target_changelevel`); `coop`: `info_player_coop`; DM family: at least two `info_player_deathmatch`; `ctf`: both flags and both teams' spawn classnames; `tdm`: n/a |

Compile flags (unchanged): Q1 `qbsp -leaktest -noallowupgrade -notex`, `vis -threads 2`, `light -extra4
-bounce 1 -soft -threads 2 -nodefaultpaths`; Q2 the same with `-q2bsp -nodefaultpaths -path <baseq2>` and no
`-notex`; Q3 `q3map2 -bsp -leaktest`, `-vis -saveprt`, `-light -samples 2 -filter -bounce 1 -patchshadows`
with a private `-fs_homepath`, then `mbspc -bsp2aas` and `-aasinfo`.

Each case directory holds the map, report and compiled BSP, the AAS (Q3), a private `q3home/`, `logs/` with
every stage's command line, exit code and wall time, and `result.json`.
