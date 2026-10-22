# Architecture

## Design rule

Probabilistic models may propose constrained intent. Deterministic project code owns topology, integer coordinates, convex geometry, title-specific entity/material mapping, validation, repair, and serialization. A model cannot emit native classnames, arbitrary entity properties, brush planes, or map text.

## Generation and M3 analysis boundaries

```text
free-form prompt
    -> PromptSpec normalizer (authoritative request)
    -> rules/v1 OR constrained llama.cpp/GGUF SemanticPlanner
       -> request-specific GBNF -> strict JSON parser -> semantic validator
       -> bounded full-plan revision with structured issues
    -> seeded integer LayoutGenerator candidates
    -> ggml feature tensor + candidate ranker
    -> typed placement allocation
    -> LayoutValidator <-> LayoutRepairEngine (fixed point/budget)
    -> MapBuilder
       -> free-space union boundary -> local shell brushes
       -> title/dialect profile -> entities, solids, projections, metadata
       -> stable source-to-map-node manifest + lowering statistics
    -> generated-profile MapDocument validation
    -> canonical Layout SHA-256
    -> player/Q1-Q2 monster navigation + visibility + analytical lighting
    -> QualityAnalyzer timing/monster evidence + advisory score
    -> canonical .map writer + schema-v4 JSON report
```

The v0.4.0/M3 pipeline runs the analyzers only after structural validation and `MapBuilder` succeed. Their findings and quality score remain advisory, but analyzer execution, caps, and exceptions are mandatory fail-closed stages: failure aborts generation before map emission. Typed localized repair remains an explicit source-level API:

```text
Layout + SHA-bound analyses + optional normalized compiler pointfile
    -> optional LeakDiagnosisResult
    -> QualityAnalyzer verifies SHA identity and independently reruns every supplied analyzer
    -> typed QualityRepairProposal evaluated against a copy
       -> preconditions + structural/identity/navigation/lowering gates
       -> configured runner policy + integrity envelope + host authentication for a local seal
       -> accept only a strictly better candidate, otherwise retain the baseline
```

`PromptSpec` fixes normalized game, mode, counts, theme, seed, and hard request bounds. `SemanticPlan` is a separately versioned, coordinate-free IR for route, symmetry, lighting, intensity, verticality, and generic room roles. Its grammar and validator prevent native syntax from crossing the probabilistic boundary.

`Layout` is the authoritative geometry/gameplay IR. It uses signed 32-bit grid coordinates; strong, nonzero `RoomId`, `ConnectionId`, `SolidId`, and `PlacementId` types; explicit passage-volume chains; bounded box or convex-polyhedron solids; typed material/purpose/ownership/contents roles; and typed placements and links. Plane triples use a documented outward-normal convention, and convex validation returns aggregate bounds plus an interior witness rather than accepting merely plausible planes.

`MapDocument` is the syntax IR. Ordered properties and primitives preserve source order and duplicate epairs; brushes retain three plane points, the dialect-specific texture projection, and optional Q2/Q3 metadata; Q3 patches retain odd width-major control grids. The bounded parser can retain an untouched pristine source, while mutation invalidates that fast path. The canonical writer produces a deterministic parse/write fixed point in all four dialects.

This split lets semantic or layout repair operate without knowing native syntax and lets source-map tooling preserve syntax that would be invalid to invent in generated output.

## Free-space-union lowering

Rooms and explicit passage boxes describe playable free space, not prebuilt walls. `MapBuilder` sorts all input collections by stable identity, deduplicates identical free volumes, and coordinate-compresses candidate faces. An elementary face cell is emitted only when it is on the boundary of the whole free-space union; adjacent cells are merged deterministically and extruded outward by the configured shell thickness.

That boundary construction handles partial openings, multiple openings, bends, T/cross junctions, duplicates, and contained volumes without sealing an interior portal or wrapping the level in one giant box. Edge/point-only contacts, detached passage chains, shell-clearance intrusion, coordinate overflow, excessive face-cell counts, and shell/total brush caps fail explicitly.

Typed solids then lower as structural world brushes, Q1/Q2 `func_detail` groups, Q3 world brushes carrying consistent `C_DETAIL`, or placement-owned mover/trigger geometry. A centralized title profile owns materials, projections, Q2 liquid contents values, Q3 metadata, native placement classnames, and link names. Q2 teleporters deliberately use the stock point-entity contract rather than copying the Q1/Q3 brush-trigger shape.

Every semantic room, connection, solid, and placement receives a manifest entry, including a stable empty entry when a contained source contributes no exposed nodes. Map-node IDs are assigned once in deterministic preorder after lowering.

## Typed construction

The M2 construction layer accepts uniquely keyed requests and sorts them before allocating IDs, so request-vector and existing-layout vector order do not affect the result. It produces:

- exact integer-grid stair runs and validated convex ramps;
- segmented convex arches and curved-wall brush approximations;
- structural water, slime, and lava volumes;
- typed door, lift, and teleporter placement assemblies;
- format-neutral mover directives carrying travel delta, speed, and wait; and
- Q3 patch-veneer extras with a stable source `SolidId`, odd width-major integer controls, fixed-point texture coordinates, and explicit hidden collision/backing brushes.

Patch grids obey the portable 3–31 odd-dimension bound. A checked `apply_to(Layout&)` rejects stale ID bases, collisions, invalid ownership/links, or mutated geometry and canonicalizes appended solids/placements by ID. The default prompt generator does not yet choose every construction request; this layer is a deterministic capability boundary, not a claim that every generated map contains every motif.

The three-argument `MapBuilder` overload accepts a `ConstructionResult` only when every generated solid (including curve backing) and placement is present unchanged in `Layout`. Detail-owned veneers emit as `worldspawn` `patchDef2` only in the two Q3 dialects; their hidden backing remains collision geometry. Unique mover directives are checked against typed destination/delta/box extent before lowering speed, exact decimal wait seconds, door angle/lip, or lift height.

## Map-document validation profiles

The syntax-preserving profile accepts parseable legacy distinctions that must survive import, including duplicate epairs and optional metadata. It still checks bounds, projection shape, patch dimensions, brush convexity, and material/token safety when canonical output is requested.

The generated-document profile is deliberately narrower: exactly one first `worldspawn`, exactly one `classname` per entity, unique generated keys, exact Valve 220 `mapversion` rules, no Q3 `mapversion`, required Q3 contents/surface/value triplets, consistent detail contents across each brush, and at least one world brush. `MapBuilder` always runs this profile before returning a document.

The `canonicalize` command uses an explicit dialect and bounded parser, discards pristine-source mode, and writes canonical output to a distinct path. It does not guess a dialect or silently translate between games.

## M3 navigation, visibility, and lighting APIs

`NavigationAnalyzer` coordinate-compresses room and explicit-passage free space, expands obstacles by a selected title/agent hull, and emits stable areas, placement bindings, and directed walk, step, jump, drop, door, lift, and teleporter edges. Player profiles use the official title hulls and 18-unit step behavior; Q1/Q2 also expose conservative small- and large-ground-monster profiles. Shortest-path and reachable-node queries are bounded. Invalid geometry, inadequate clearance, unresolved typed transitions, unbound gameplay placements, and unreachable spawns/objectives/monsters are typed error findings, while invalid options, arithmetic overflow, or capacity exhaustion fail closed with `NavigationError`. Q3 monster semantics are deliberately absent.

`VisibilityAnalyzer` classifies a segment as visible, occluded, or outside the union of room/passage free space. Coverage and box occluders are exact; a convex solid is represented by its validated aggregate bounds and marks affected results as conservative. Conservative occlusion is treated as potentially visible for exposure/hostile-LOS/crossfire purposes and cannot establish cover, so an aggregate box never creates a false safety proof. Its stable outputs include placement line of sight, room exposure, exact cover, integer-CORDIC crossfire angles, components/articulation, bridge/chokepoint/readability, and spawn safety. Missing mode-required spawns are errors; hostile LOS, close separation, wide crossfire, and missing cover are warnings. When missing-cover reporting is enabled, the cover requirement also participates in the `safe` verdict. Count, coordinate, segment-test, crossfire, cover, and finding caps fail closed with `VisibilityError`.

`LightingAnalyzer` deduplicates repeated passage boxes, samples rooms, unique passage chains, and gameplay placements, then submits every sample/light pair to one bounded visibility batch. Each visible typed light contributes a deterministic integer metric based on intensity, perceptual color luminance, inverse-square attenuation, and an explicit near-distance clamp. It records visible/occluded/outside/conservative traces, critical-sample readability, connection minima/maxima, and contrast. No lights or an unreadable critical placement are errors; ordinary unreadable samples/connections and excessive contrast are warnings. This metric is deliberately not physical lux and is not a substitute for compiled lightmaps; hash-pinned quick/final compiler gates provide that separate evidence. Lighting limits are cross-validated against their nested visibility limits, and invalid thresholds or sample/light/visibility/finding cap exhaustion fails closed with `LightingError`.

Analyzer findings are advisory evidence: they do not modify `Layout` or by themselves change CLI status. Their execution contract is not optional inside the generation pipeline, however; invalid options, overflow, cap exhaustion, or an analyzer exception aborts the run before output. Navigation, visibility, and lighting error findings feed the quality score without being promoted automatically into structural validation failures.

The source-level core entry points are `NavigationAnalyzer::analyze` with `NavigationOptions`/`NavigationAnalysis`, bounded `shortest_navigation_path` and `reachable_navigation_nodes`; `VisibilityAnalyzer::classify_segment`/`classify_segments`/`analyze` with `VisibilityOptions`/`VisibilityAnalysis`; and `LightingAnalyzer::analyze` with `LightingOptions`/`LightingAnalysis`. Each analysis carries the lowercase canonical layout SHA-256 used by downstream stale-evidence checks. These headers define project-internal library boundaries; the installed runtime/API surface is the `qmg` executable rather than a packaged C++ SDK. The install tree also carries the project/dependency notices and the semantic-plan schema/grammar contracts, but no public C++ headers.

## M3 leak diagnosis and typed quality repair

`parse_compiler_pointfile`/`load_compiler_pointfile` accept an explicit Q1, Q2, or Q3 dialect and strict three-coordinate lines regardless of whether the compiler called the file `.pts` or `.lin`. Input bytes, line length, point count, coordinate magnitude, finite conversion, and fixed-point overflow are bounded. Consecutive duplicates are removed, coordinates normalize to 1/1024 world-unit integers, and the witness direction is canonicalized, producing a direction-invariant fingerprint. Malformed, oversized, empty, or degenerate input is rejected rather than partially trusted.

`build_layout_exterior_grid` creates a bounded coordinate-compressed cell representation of the room/passage union and flood-labels exterior, enclosed-void, and interior cells. `diagnose_layout_leak` uses it to prove the first witness crossing of an exposed local face and attributes that crossing to exactly one room or connection, axis, and minimum/maximum face. Edge/corner hits, coincident ownership, no crossing, invalid geometry, overflow, and cap exhaustion produce typed non-success results. A successful result proposes a small outward structural wall slab with canonical layout SHA-256 plus layout/witness fingerprint, expected owner/face/bounds, exterior-cell proof, baseline rank/count metadata, and a mandatory compiler-recheck bit. It never creates a global wrapper. `local_seal_proposal_preconditions_hold` proves only that these local preconditions still match; it cannot prove a compiled artifact is sealed.

`QualityAnalyzer::analyze` consumes `QualityAnalysisInputs` plus bounded `QualityOptions` and composes structural validation, leak status, player navigation, visibility, lighting, travel timing, and Q1/Q2 small/large-monster reachability into `QualityAnalysis`, typed metrics/findings, and a lexicographic `QualityScore`. It requires each supplied artifact to carry the exact lowercase `canonical_layout_sha256(layout)`, then independently reruns player/monster navigation, visibility, lighting, and any supplied diagnosed leak under the exact embedded options and requires full typed-payload equality. The returned `QualityAnalysis` repeats the same canonical layout SHA-256. Null leak evidence means “not evaluated”; supplying an ambiguous, invalid, or otherwise non-diagnosed leak result rejects. `quality_layout_fingerprint` is a domain-separated 64-bit derivative of that bounded canonical SHA-256 and is only a lightweight edit-precondition token, while the SHA-256 is the artifact identity. `quality_score_strictly_better` exposes the comparison policy. Q3 rejects invented monster evidence and omits monster timing.

Timing/monster findings are advisory analysis evidence. `QualityRepairEngine::evaluate` accepts a `QualityRepairProposal` containing exactly one `AddOrAdjustLightRepair`, `MovePlacementRepair`, `ReplaceConnectionPassageRepair`, `AddCoverSolidRepair`, or `LocalSealRepair`. It evaluates a copy and returns an accepted `Layout` only when the proposal fingerprint is current, no new structural error or placement-identity loss appears, no established player/monster path is lost, no navigation error is added or exchanged for a different identity at the same count, lowering still succeeds, and the score strictly improves. `QualityRepairOptions::dialect` optionally binds lowering and compiler evidence to an exact title-compatible dialect—including Q3 brush primitives—or selects the title default; an incompatible dialect rejects. When supplied, `QualityRepairOptions::construction` uses the construction-aware `MapBuilder` overload so patch veneers and mover directives survive candidate lowering.

A seal is harder. `QualityRepairOptions::compiler_recheck` must define `CompilerRecheckRequirements` for a runner, exact dialect/pointfile dialect, and executable/options/invocation hashes. `CompilerRecheckEvidence` binds its schema and runner to the proposal; baseline/candidate fingerprints and canonical layout SHA-256 values; exact map dialect; canonical lowered map hash and byte count; compiler executable/options/invocation hashes; BSP hash and byte count; diagnostics hash; pointfile dialect and witness; compiler success; postcompile pointfile absence; and an explicit leak-free result. `compiler_recheck_envelope_sha256` is an unkeyed integrity digest over those fields in a canonical envelope capped at 4,096 bytes; it is explicitly not authentication. Runner IDs are capped at 256 bytes and opaque `verifier_authentication` at 16,384 bytes. The caller must also supply a non-null `CompilerRecheckVerifier`; its host implementation authenticates the envelope through a signature, trusted IPC token, or other out-of-process trust mechanism and must return true. Missing/mismatched evidence, a missing/false/throwing verifier, or a cap violation rejects. Core provides neither key management nor automatic compiler execution. A candidate may still contain pre-existing findings; acceptance means a strict ordered improvement under these gates, not a claim of perfection.

## Inference boundary

The dependency remains llama.cpp v0.3.0 with bundled ggml v0.22.0. ggml executes the deterministic candidate-scoring graph. The model-neutral `TextModel` boundary and llama.cpp adapter accept explicit single-file, decoder-only GGUFs with supported chat templates. The adapter is CPU-only by default, uses a fresh context and grammar sampler per attempt, and rejects split/model-incompatible files, context overflow, invalid grammar, incomplete truncation, and identity changes across load.

The request-specific grammar fixes object shape, integer syntax, exact room-role cardinality, route feasibility, drift bounds, and directly expressible SP/arena/CTF contracts. A dependency-free parser and semantic validator independently reject malformed JSON, duplicate/unknown/missing fields, invalid UTF-8/escapes, excessive bytes, and schema/range failures. No weights are bundled or downloaded.

## Determinism and provenance

Determinism is enforced at each project-owned boundary: explicit request/candidate seeds, strong semantic IDs, sorted construction/lowering/analysis inputs, integer or fixed-point geometry, bounded iteration/cell/brush/query counts, deterministic preorder map IDs, canonical layout/envelope hashing, and canonical text rendering. `canonical_layout_sha256` uses a domain-separated typed binary encoding capped at 64 MiB; it is not process-locale-dependent JSON. Tests shuffle top-level `Layout` collections and require identical graphs, metrics, diagnoses, hashes, map bytes, and manifest contents; canonical parser/writer tests require idempotence. Report numeric rendering uses the classic locale and rejects non-finite rank values, keeping emitted JSON valid and independent of the process-global locale. External compiler stages are hash-pinned and single-threaded where supported, but the project does not claim their artifact bytes are deterministic.

The schema-v4 generation report records request provenance fields (source prompt, game, mode, theme, request seed, and candidate seed), canonical layout SHA-256, semantic plan, planner backend/status and hash-chained attempts, model/tokenizer and dependency identity, ggml ranking data, structural repairs and final validation issues, selected map dialect, lowering status, manifest entry/node counts, and free-volume/shell/entity/brush/face statistics. `analysis.advisory` is true for finding policy and `analysis.execution_required` is true for the fail-closed analyzer execution contract. Player and optional monster navigation, visibility, lighting, and quality each serialize availability, bounded summaries, scores where applicable, and typed findings; all four analysis kinds repeat their bound layout SHA-256. For an available quality artifact, `render_report` independently reruns quality from the stored upstream analyses and requires full `QualityAnalysis` equality, preventing a copied SHA from covering mutated quality content. `analysis.quality.summary.leak_diagnosis_evaluated` is false in normal pipeline reports because generation has no local compiler-pointfile diagnosis; therefore a zero leak-error score does not claim proven leak freedom, and this field does not cover later authenticated compiler evidence. An analyzer exception prevents report/map emission. Repair proposals, runner policies, integrity envelopes, verifier authentication, and compiler results are not produced automatically by generation. The complete source-to-node manifest remains an in-memory `MapBuildResult`; the report records its counts rather than serializing every mapping or a final map-artifact hash.

## Remaining boundary

M3 provides bounded SHA-bound and independently rechecked analyzers, advisory schema-v4 findings, typed local proposals, host-authenticated compiler-evidence score/regression acceptance, global compiled AAS evidence, and representative compiled VIS/light evidence. These are static/compiler checks, not an engine playtest or proof of a finished, balanced map. Licensed-map ingestion/training is M4; encounter/resource simulation, production balance/art/performance, runtime telemetry, and the broader production loop remain M5. Q3 output targets tournament/duel/DM/team/CTF arenas; it is not a monster campaign and no stock Q3 changelevel entity is invented.
