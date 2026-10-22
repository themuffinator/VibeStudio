# Default multiplayer portfolio export

`qmg_mp_portfolio_export` runs the real `GenerationPipeline` with its default
candidate count, rules planner, ggml selection and repair options. It exports the
selected pipeline MAP and `.qmg.json` bytes without changing geometry, materials,
entities, title or the planner/ranker trace. This is an adapter for portfolio
inspection, not a separate recipe or alternative candidate selector.

Run from the project root after building the registered target:

```powershell
cmake --build --preset windows-clang --target qmg_mp_portfolio_export
./build/windows-clang/qmg_mp_portfolio_export.exe `
  out/mp-portfolio/prompts/rooms6-seed42.txt `
  out/mp-portfolio/generated/rooms6-seed42 `
  qmg_weft "Weft"
```

The optional title changes only the analytical `layout.json` name. The map name
selects output filenames; it does not rewrite worldspawn or any native class.
Use the same prompt with ordinary `qmg generate` to compare exact MAP/report
bytes. No game, browser, compiler, input device or external asset is opened by
the exporter.

The current contract accepts Q2 deathmatch/duel layouts with AABB solids. It
rejects convex solids rather than flattening them, and rejects lift/teleporter
assemblies whose display metadata it cannot certify. All typed placements,
including lights, remain in the layout export. Native class annotations must
come from the unchanged MAP/BSP. Room tags, solid IDs, material roles, ownership,
contents and placement attributes are retained.

Floor levels come from room floors and solid raised-platform tops, excluding
intermediate stair treads. Stair runs are inferred only from unambiguous axial
36-unit tread chains with 16-unit rises, a shared base and cross span, exact
adjacency and endpoints at declared floor/platform levels. Both positive and
negative X/Y runs are ordered uphill. Ambiguous, disconnected, reversed or
unsupported tread geometry fails export; this inference is not native movement
or full approach/landing evidence. The output includes each run's actual solid
IDs so a later compiled audit can check the same geometry.

Ordinary counts independently traverse only walk/step/door edges in the selected
pipeline's navigation graph. Required, reachable and missing gameplay pairs are
reported separately; a missing pair is retained as evidence rather than silently
repaired or used to choose a different candidate. The original `.qmg.json`
retains the authoritative complete analyses and costs.

Outputs must lie in a named directory under this project's `out/`, with no
filesystem links in the path or existing subtree. MAP/report/layout targets must
be absent or byte-identical independent regular files. Different files are never
overwritten; use a fresh case directory for another generator revision. Existing
compiler/preview files may coexist and are left untouched. A failed write may
leave partial output; the exporter performs no cleanup or hidden retries.

Compilation, compiled anchors, independent geometry checks and browser preview
are subsequent steps. `render_layout.py` additionally requires
`compiled-analysis.json`; the exported layout alone is not rendered or presented
as a playable-quality result.

The retained parity control (`out/mp-portfolio/export-parity/parity.json`, upstream study evidence, not imported)
compares MAP and report bytes with the ordinary CLI for the same default
rooms6/seed42 prompt. Both match exactly, including the ggml/planner report.
Five exporter controls (`out/mp-portfolio/export-parity/export-controls.json`, upstream study evidence, not imported)
check an unchanged rerun and reject a different display export, traversal or
reserved filenames, and output outside the project output area. The existing
files' bytes and modification times stay unchanged in all five cases.

## Candidate and topology follow-up

`qmg_mp_candidate_trace <prompt-file>` writes six candidate seeds, families, room edges and rank scores to standard output. It independently runs the ordinary pipeline and checks selected seed/score; without repairs, selected ranked canonical geometry must match too. It never exports or substitutes a candidate. Retain the trace beside the final layout, as in [Splice](../../docs/Q2_SPLICE_STUDY.md).

`python -B -m tools.mp_portfolio.topology_compare --output out/splice-portfolio` compares retained matched baselines and final files. It verifies actual MAP/BSP bytes and binds primary and hero traces to final canonical identity. Multigraph identity retains parallel doorways; unweighted simple-room shortest paths count each adjacency once and divide credit across ties. Room-removal counts exclude endpoints in the removed room. These descriptors do not measure running time, exposure or multiplayer quality. A room-only slicing strip excludes walls/passages and is not a playable-space certificate.

The [Lattice follow-up](../../docs/Q2_LATTICE_STUDY.md) adds all eight ordered feature inputs, request weights, float products, rank-neutral layout hashes and selection/tie policy diagnostics to the tracer. `python -B -m tools.mp_portfolio.ranking_compare --output out/lattice-portfolio` binds the 16 frozen traces and 32 diagnostic MAP/report parity pairs before comparing corresponding candidates. It rejects changed candidate geometry/features, unexpected weights or repairs, malformed arithmetic and invalid tie selection. Thirteen controls protect those checks. Matching marked 2–8-room Q2 multiplayer pools abstain from the volume-CV and 1,280-center-spacing preferences; other inputs remain visible adequacy heuristics. Export still follows the ordinary pipeline, and comparison descriptors do not establish quality.

## Explicit intent and placement comparison

The [Braid comparison](../../docs/Q2_BRAID_STUDY.md) retains baseline A,
intent-only B and final C for 16 identical request texts. Requested
`layout_intent` is carried in prompt/planner/report/trace identity. Explicit
`wrapped_center` constrains every candidate to the pinwheel family only in
supported Q2 DM/duel5–8-room requests; no ranker bonus is added. Generic loops
retain unspecified intent.

`python -B -m tools.mp_portfolio.braid_compare --verify` independently
rederives the retained comparison. It binds stage source/executable snapshots,
prompts, selected traces, MAP/BSP identities and compiled analyses. A→B permits
construction changes. B→C instead requires all 96 spawn-neutral candidate
hashes, exact selected world solids and nonspawn entities, authored fields
except spawn origins, pairwise blocked-ray preservation and complete ordinary
graph pairs. Only selected candidates are compiled; the candidate hash check
does not imply independent compilation of all 96 candidates.

Report primary, holdout and example strata separately, including envelope
growth, graph variety loss and all warnings. The 42-file unspecified parity
snapshot belongs to stageB before local spacing; the final12-file CLI/legacy
record is separately bound. These checks concern generated and compiled static
evidence. They do not execute respawn selection, native journeys or gameplay.

## Upper-link comparison

`python -B -m tools.mp_portfolio.hinge_compare --verify` checks the retained
baseline and `generated-refined` Hinge stages. It also retains the first
`generated` stage's candidate offers and no-link example. All three stages use
the same 16 prompt texts and seeds; no intermediate export is overwritten.

Trace comparisons retain all 96 candidate observations. Those records expose
room IDs and appended passage endpoints, not every candidate's complete solid
geometry. Structural preservation is checked from actual selected exports
when their candidate seeds match, including original rooms, stairs, solids,
lower passages and exact added slabs. Placement and compiled entity changes
remain explicit. An added parallel upper edge must not be described as new
unique adjacency or reduced room hops.

The comparison binds selected BSPs, full compilation, gameplay support,
ordinary graph pairs, material physics, warnings and source snapshots. Actual
upper support and closed-opening controls are separate artifacts. They do not
make a source passage floor into physical support or turn a graph path into a
native journey.

The frozen Hinge comparison binds 558 inputs and preserves 16 selected seeds.
First-stage 67/96 offered links and 10/16 selected links remain separate from
the phase-refined 77/96 and 12/16 results. All selected compiled envelopes
remain unchanged, while allocation moves and additional lighting findings are
retained. The rule update is reproducible and read-only-checkable with
`python -B out/hinge-portfolio/update_knowledge.py --verify`; it derives 54 cards
from the frozen 51-card baseline without modifying historical cards.
