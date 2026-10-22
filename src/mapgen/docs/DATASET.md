# Dataset and learning plan

Human-authored maps can teach topology, pacing, item economies, sightline patterns, height use, encounter composition, and construction idioms. They can enter training or retrieval only when the project has permission for that use.

## Admission manifest

Every source item requires a machine-readable record containing:

- canonical source URL and retrieval date;
- author and map title;
- exact licence text/identifier and evidence URL;
- whether modification, redistribution, and ML training are allowed;
- original archive/file hashes;
- game, mode, map dialect, compiler/tool versions, and required mods;
- which files may be redistributed and which are local-only; and
- attribution text that must accompany derived artifacts.

Unknown, contradictory, noncommercial-only, “no derivatives,” or missing terms quarantine the item. Commercial id Software data, installed BSPs, and decompiled maps are never assumed trainable merely because they are locally readable.

Ingestion validates magic bytes and parses content; it does not trust file extensions. Archives are scanned before extraction and paths are normalized to prevent traversal.

## Processing stages

1. Hash, licence-gate, and preserve the immutable source record.
2. Parse source `.map` when available; otherwise keep binary-only material in a compatibility-only partition.
3. Normalize brushes/entities into the title-aware IR without dropping author, source, or stable object identity.
4. Compile with a pinned external toolchain and retain structured diagnostics.
5. Derive topology, navigation, visibility, lighting, encounter, item-control, and construction features.
6. Split by author and map family before training to prevent near-duplicate leakage.
7. Produce retrieval documents, contrastive/ranking examples, semantic-plan pairs, and repair examples.
8. Train outside the runtime repository, evaluate against held-out authors, quantize an approved model to GGUF, and record complete model/data provenance.

## Learning strategy

The first learned component is candidate ranking: expert maps and validated procedural perturbations yield pairwise “better/worse” layout examples. A constrained planner then learns prompt-to-semantic-plan and issue-to-revision tasks. Geometry remains deterministic. Retrieval can supply title/mode-specific precedents without embedding copyrighted map content in prompts or outputs beyond allowed terms.

Evaluation measures compiler success, sealing, navigation, objective reachability, MP route/item/spawn metrics, SP pacing/resource metrics, novelty, and blinded human review. Training loss alone is not a level-quality claim.

The v0.4.0/M3 navigation, visibility, analytical-lighting, pointfile/leak, timing, monster-reachability, and quality-score APIs provide deterministic feature and repair-evidence boundaries for stage 5. Their default thresholds are project heuristics rather than labels learned from authors.

The first executable M4 foundation is the [reference-map study regimen](MAP_STUDY.md). `python -B -m tools.map_study` now provides a bounded IBSP38/EAAS reader, local provenance registry, serialized static descriptors, provisional author/family partitions, a 26-map curriculum, 15 design-rule hypotheses, descriptive reference profiles and a hash-pinned generator/compiler comparison runner. Binary-only references remain local study material; they do not become training or raw-retrieval examples. Unknown rights stay explicitly disabled. AAS sidecars are not treated as authenticated navigation evidence.

M4 still owns reviewed licence admission, authored-source IR conversion, human/experimental labels, genuinely leakage-audited splits, retrieval/training datasets, fitting weights, evaluation and model cards. M5 owns runtime playtest and production-quality evidence. The reference-study profiles describe measurements; they are not trained quality models.
