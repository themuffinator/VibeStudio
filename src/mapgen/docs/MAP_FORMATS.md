# Map format contract

`Layout` is authoritative for generated geometry and gameplay intent. `MapDocument` is authoritative for parsed or lowered source syntax. Text `.map`, compiled BSP/AAS, and later navigation artifacts are outputs or validation evidence; compilation does not mutate the layout IR.

| CLI format | Game | Brush/projection syntax | Generated metadata | Structural compiler evidence |
| --- | --- | --- | --- | --- |
| `q1-valve220` | Quake | Valve 220 axes, `mapversion 220` | no Q2/Q3 face triplet | BSP29 via ericw-tools qbsp |
| `q2-valve220` | Quake II | Valve 220 axes, `mapversion 220` | optional contents/surface/value preserved; explicit liquid contents when generated | IBSP38 via ericw-tools qbsp `-q2bsp` |
| `q3-legacy` | Quake III Arena | legacy Q3 projection plus contents/surface/value | explicit triplet on every generated face | IBSP46 via q3map2; EAAS5 via BSPC |
| `q3-brush-primitives` | Quake III Arena | `brushDef` matrix projection | explicit triplet on every generated face | IBSP46 via q3map2; EAAS5 via BSPC |

`generate --format` accepts exactly those four names. The default follows the selected game (`q3-legacy` for Q3), and a game/dialect mismatch is rejected. `canonicalize` requires an explicit format and distinct `.map` input/output paths; it does not guess or translate dialects.

## Generated lowering

`MapBuilder` converts conventional outward `Layout` plane triples into the point order expected by Quake map syntax using a verified interior witness. Boxes and validated convex polyhedra share that path. Generated projections are deterministic: axis-selected Valve 220 bases for Q1/Q2, a fixed Q3 legacy projection, and a nonsingular 1/64 brush-primitive matrix.

Materials and native entities are selected only by the centralized game profile. Q3 material tokens are relative (for example `base_wall/...`), preventing the invalid `textures/textures/...` expansion seen when already-prefixed tokens are passed to q3map2. Material tokens containing comment delimiters are rejected before canonical writing.

Ownership lowers as follows:

- structural brushes and liquids stay in `worldspawn`;
- Q1/Q2 detail brushes are grouped in `func_detail`;
- Q3 detail brushes stay in `worldspawn` with `C_DETAIL` set consistently on every face; and
- placement-owned brushes stay with their typed door/lift/trigger entity.

Q2 water, slime, and lava use contents values 32, 16, and 8 respectively. Q1 derives liquid behavior from its material and carries no Q2/Q3 metadata. Q3 generated faces always carry contents/surface/value, including consistent detail contents. The source parser/writer preserves Q2 metadata when present and preserves its absence when omitted; it does not replace unknown legacy values with zeroes.

Typed placements map separately for each title. In particular, the stock Q2 teleporter source is a point `misc_teleporter` linked to `misc_teleporter_dest`, unlike the brush trigger used by Q1 and Q3. Q3 “single-player” emits tournament/arena placement semantics. There is no stock Q3 campaign changelevel mapping.

## Patches and curves

`MapDocument` parses, validates, preserves, and canonically writes Q3 `patchDef2` primitives with odd 3–31 width/height and width-major control storage. The construction layer represents a patch veneer with integer positions, fixed-point texture coordinates, a stable source ID, and hidden convex collision/backing solids. After those backing solids are applied unchanged to `Layout`, the construction-aware `MapBuilder` overload emits the veneer in `worldspawn` for either Q3 dialect and attributes its patch/control nodes to the source ID. Q1/Q2 reject veneer extras and use segmented convex brush approximations for the same curved-wall intent.

The Q3 legacy and brush-primitive profiles differ only in brush representation; patches remain separate primitives. A document cannot mix legacy and brush-primitive brush models. BSP2, 2PSB, Qbism, BSPX, and other extensions are not silently selected.

## Parser and writer behavior

The bounded parser supports ordered entity properties, duplicate epairs, brushes, optional Q2/Q3 metadata, and Q3 patches. Default limits include 16 MiB of source, 4 KiB tokens, 512 faces per brush, 31 controls per patch dimension, and one million total nodes. It rejects partial metadata triplets, malformed projections, zero Valve scales, singular brush-primitive matrices, invalid patch dimensions, embedded NUL, unsupported includes, excessive nesting/counts, and compiler-incompatible line breaks.

An unmodified parsed document can retain and re-emit its pristine source byte-for-byte. Any mutable access invalidates that mode. Canonical mode normalizes numbers (including negative zero), indentation, and ordering while preserving the document’s property/primitive order. All four checked-in golden dialects satisfy parse → canonical write → parse → identical canonical write.

Two validation profiles serve different purposes:

- `syntax_preserving` accepts parseable legacy distinctions such as duplicate epairs and optional metadata so import is lossless; and
- `generated_document` adds the strict worldspawn/classname/mapversion/metadata/detail rules required of project output.

`MapBuilder` validates under `generated_document` before returning. The standalone canonicalizer uses the parser and canonical writer, not the narrower assumption that its input was generated here.

## Compiler gates

The Q1 gate generates six-room SP and 32-room DM fixtures. The Q2 gate generates six-room SP and 32-room CTF fixtures. Both require an exact caller-supplied `qbsp` hash, reject compiler leak/invalid-brush diagnostics, require neither `.pts` nor `.lin`, require a nonempty `.prt`, disable BSP upgrades, and inspect the required nonempty BSP29/IBSP38 lump envelopes. Project-authored open-ceiling negative controls must produce leak diagnostics and nonempty `.pts`. If all four optional VIS/light path/hash settings are supplied, every Q1/Q2 fixture runs both quick and final VIS/light. Those stages run single-threaded where supported, but their artifact bytes are not claimed deterministic. The result must retain nonempty visibility and lighting data; Q1 additionally requires a valid QLIT version-1 RGB sidecar whose payload matches the BSP lighting samples.

Each Q3 dialect independently generates duel-4, CTF-8, and DM-32 fixtures. q3map2 must emit sealed IBSP46 plus a nonempty portal file and no `.lin`. Every scale receives single-threaded quick VIS/light and the duel representative also receives final VIS/light; every lit stage must report positive illuminated luxels and lightmaps, and compiled draw-surface, lightmap, light-grid, and visibility lumps must remain nonempty. BSPC must build EAAS5, and its own `aasinfo` reader must report positive and consistent areas/settings, reachability size, nodes, portals, and clusters. Its full reachability-category accounting must match `reachabilitysize - 1` after the sentinel and include representative walk, barrier-jump, and walk-off-ledge counts. A project-authored room expressed natively in each Q3 brush dialect with a deliberately missing ceiling must produce both a leak diagnostic and nonempty `.lin`, proving the negative control. AAS supplies global compiled topology/category evidence; entity-to-entity reachability comes from the project navigation analysis. These are compiler/structural, compiled VIS/light, and AAS-content gates, not an engine playtest or a proof of balanced bot routes.

Compiler pointfiles are diagnostic evidence, not source-map syntax. ericw-tools normally writes `.pts` and q3map2 writes `.lin`; the M3 ingestion API accepts an explicit Q1/Q2/Q3 dialect and bounded coordinate-triplet content rather than trusting an extension. It canonicalizes finite coordinates to 1/1024 world-unit fixed point and produces a direction-invariant witness fingerprint. `local_seal_proposal_preconditions_hold` proves only local applicability. Acceptance requires recompiling the canonical lowered candidate under configured `CompilerRecheckRequirements` and supplying matching `CompilerRecheckEvidence` bound to proposal, baseline/candidate canonical hashes, exact map and pointfile dialects, source/compiler invocation/BSP/diagnostics identities, witness, compiler success, pointfile absence, and explicit leak freedom. `compiler_recheck_envelope_sha256` provides unkeyed field integrity, not authentication; a non-null host `CompilerRecheckVerifier` must authenticate the envelope and return true. Core neither manages trust keys nor invokes the compiler. `QualityRepairOptions::dialect` selects that exact title-compatible lowering dialect or defaults by title; Q3 brush primitives are supported and an incompatible dialect rejects.
