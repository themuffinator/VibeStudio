# ericw-tools High-Value Resolution Plan

Plan date: 2026-05-02

Source audit: `docs/plans/ericw-tools-open-issues-audit.md`

Integration model: `docs/COMPILER_INTEGRATION.md`

Agent worklog: `docs/plans/ericw-tools-ralph-worklog.md`

Remaining-pass matrix:
`docs/plans/ericw-tools-remaining-bugs-resolution.md`

This plan turns the high-value ericw-tools open-issue audit into
user-facing VibeStudio work. VibeStudio invokes ericw-tools as external
executables, so resolution means one of:

- a wrapper or manifest behavior that makes a compile safer or reproducible;
- a preflight diagnostic that prevents a known bad input from reaching the
  compiler;
- a known-issue diagnostic that explains a version-specific or option-specific
  risk;
- a regression fixture that records current behavior while the real compiler
  fix remains upstream.

## Current Implementation Status

Implemented on 2026-05-02:
- `src/core/compiler_known_issues.*` provides a high-value ericw-tools issue
  catalog with upstream issue IDs, dedupe clusters, affected tools/profiles,
  match keywords, severity, warning text, and suggested local action.
- `src/core/ericw_map_preflight.*` provides conservative Quake `.map`
  preflight validation for path privacy, long values, escape sequences,
  external-map/prefab hazards, grouped/toggled light conflicts, region risks,
  brush-entity origins, Phong risks, `_minlight`, `_sunlight2`, and
  `world_units_per_luxel`.
- Compiler plans and manifests now carry separate `knownIssueWarnings` and
  `preflightWarnings` fields, while preserving the existing combined warning
  list for compatibility. CLI plan JSON exposes both categories separately.
- Compiler runs surface plan and preflight warnings before process launch,
  refuse missing working directories, and run with isolated compiler temporary
  directories recorded in manifest environment provenance.
- Smoke coverage now includes known-issue catalog matching, map preflight
  validation, dotted filenames, isolated temp environment, manifest warning
  propagation, and compiler profile integration.

Remaining upstream-only items are still documented below and must not be marked
fixed by VibeStudio unless ericw-tools is updated or a documented fork is
created.

## Remaining-Pass Acceptance

The second Ralph pass extends this plan with a 62-ID acceptance matrix in
`docs/plans/ericw-tools-remaining-bugs-resolution.md`. That matrix is grouped
by VibeStudio-owned resolution status: catalog warning, map preflight warning,
registry/helper readiness, artifact validation, or upstream-only tracked
limitation.

Keep the boundary strict:
- "Implemented" means the VibeStudio warning, readiness check, preflight check,
  or artifact gate exists in VibeStudio.
- Compiler output correctness still belongs to ericw-tools unless VibeStudio
  updates the submodule or creates a documented fork.
- If another Ralph lane has targeted but not landed a source/test change, docs
  should say "targeted in this Ralph pass" instead of presenting it as shipped.

## Resolution Boundary

| Resolution type | VibeStudio owns | VibeStudio does not own |
|---|---|---|
| Wrapper mitigation | Isolated task directories, explicit working directories, executable descriptors, output registration, cancellation, logs, manifests, and post-run sanity checks. | Changing ericw-tools argument parsing, binary names, native temp-file behavior, or build-system requirements. |
| Preflight validation | Map/entity/path checks with clickable diagnostics and suggested profile or data fixes. | Compiler parser semantics, compile geometry algorithms, lighting algorithms, or emitted BSP/BSPX contents. |
| Known-issue diagnostics | Profile-, tool-, keyword-, and argument-scoped warnings that connect compile context to upstream issue IDs. Version-specific gating is a planned refinement where reliable version data is available. | Promising that a warning-free compile is visually or physically correct. |
| Regression fixtures | Small maps and manifest expectations that show whether a selected ericw-tools version still exhibits a known behavior. | Shipping upstream test assets without license review or maintaining a fork silently. |

## Implementation Phases

### Phase 1: Wrapper Safety And Provenance

Goal: every ericw-tools run is reproducible and bounded before deeper map
analysis exists.

User-facing behavior:
- Compiler plans show executable descriptor, resolved path, working directory,
  input map, expected output, command line, and warning categories. Registry
  views can show version/help probe results separately from manifests.
- Compiler runs use isolated temporary directories through `TMP`, `TEMP`, and
  `TMPDIR`; expected-output registration only promotes files VibeStudio planned.
- Command manifests remain the canonical provenance record when upstream BSP or
  BSPX metadata is absent.
- Post-run diagnostics warn when expected outputs are missing, BSP format looks
  inconsistent with the selected profile, or profile-requested BSPX metadata is
  absent.

Issues mitigated by VibeStudio: #167, #309, #399, #415, #463, #483.

Still upstream-only: embedded compiler parameters, `LMSHIFT` generation fixes,
light-specific BSPX documentation, and native temp-dir behavior.

Tests:
- Implemented: save and reload manifests for ericw profile plans, preserve
  known-issue/preflight warning fields, run a fake compiler with isolated temp
  environment, parse diagnostics, register expected outputs, and refuse missing
  working directories before launch.
- Implemented: confirm missing outputs, wrong BSP flavor, truncated/corrupt BSP
  headers, selected lump/face-reference risks, and missing profile-required
  BSPX metadata produce post-run diagnostics.

### Phase 2: Path, Asset, And Privacy Preflight

Goal: prevent common path and asset-layout failures before the compiler starts.

User-facing behavior:
- WAD lists and entity key values are checked for absolute private paths,
  excessive length, suspicious escaping, and engine buffer risk.
- External-map references warn on absolute, backslash, or otherwise
  platform-specific path forms.
- Texture lookup diagnostics are currently known-issue/profile warnings for
  non-WAD and hardcoded `textures` folder assumptions; deeper asset-root
  resolution checks remain planned.
- Dotted filenames and destination paths are shown in the compile plan exactly
  as VibeStudio expects them to resolve.

Issues mitigated by VibeStudio: #87, #199, #201, #230, #245, #288, #293, #346,
#450, #451.

Still upstream-only: native warning text, compiler-side path resolution,
`-outputdir`/`-waddir`, hardcoded texture-root behavior, and new image loader
support.

Tests:
- Implemented: fixtures cover absolute WAD paths, long entity values,
  backslashes, dotted filenames, missing external-map classnames, and
  nonportable external-map paths.
- Planned: fixtures for map-relative versus project-relative wording and loose
  texture roots outside `textures`.

### Phase 3: Light Entity And Profile Validation

Goal: make high-value lighting risks visible where users can act on them.

User-facing behavior:
- Light inspectors and compile preflight flag toggled lights with explicit
  `style`, inconsistent grouped `START_OFF`, Q2 `angle` plus `mangle`, and
  ambiguous `_minlight` scale.
- Compile profiles and map preflight warn on implemented high-risk keywords and
  map features such as `-bouncedebug`, `-dirtdebug`, `_minlight`,
  `_sunlight2`, Q2 `angle` plus `mangle`, surface lights, Phong on brush
  entities, and bmodel dirt/minlight risks. Broader option-specific checks such
  as high `-gate`, per-light bouncescale, and switchable-shadow/skip
  combinations remain planned unless matched by known-issue keywords.
- Known-issue diagnostics include upstream issue IDs and the safest local next
  action: change map data, change profile options, try a known-good compiler
  version, or accept an upstream-only risk.

Issues mitigated by VibeStudio: #77, #122, #173, #214, #255, #256, #268, #291,
#310, #349, #351, #377, #400, #405, #416, #470, #475.

Still upstream-only: lighting math, style preservation inside the compiler,
debug image correctness, and Q2/Phong/sunlight behavior fixes.

Tests:
- Implemented: fixtures cover grouped `START_OFF`, toggled `style`, Q2
  `angle`/`mangle`, and plan/manifest propagation for known-issue warnings.
- Planned: direct fixtures for `_minlight`, per-light bouncescale, Phong
  bmodels, skip/switchable-shadow combinations, version-gated warnings, and
  GUI parity for diagnostic IDs/severity/action text.

### Phase 4: Region, Prefab, And Partial-Compile Workflows

Goal: keep quick iteration workflows honest about what they can safely compile.

User-facing behavior:
- Region plans explain whether the run is preview-only or release-safe.
- Preflight flags multiple region brushes, Q2 areaportals inside region
  workflows, rotating entities with origin brushes, and the risk that brush
  entities outside the intended region may remain in partial compiles.
- Prefab inspectors flag missing `_external_map_classname`, grouped-only
  external maps, risky rotations, entity-forwarding assumptions, and path roots
  that are not portable.

Issues mitigated by VibeStudio: #193, #194, #199, #207, #231, #327, #333, #390,
#417, #422, #444.

Still upstream-only: multiple-region support, areaportal behavior, entity
culling, external-map entity import, grouped-prefab corruption, Phong
forwarding, merged target entities, and transform-shadow correctness.

Tests:
- Implemented: fixture coverage includes rotating-door origin risk when region
  compile is requested, external-map missing classname, and external-map path
  portability.
- Planned: direct fixtures for multiple region markers, Q2 areaportals,
  grouped-only prefab content, rotation warnings, map-relative path wording, and
  unsupported entity-forwarding.

### Phase 5: Tool Discovery And Setup Diagnostics

Goal: make external executable and dependency problems understandable before
users lose time to failed compiles.

User-facing behavior:
- Tool discovery uses VibeStudio descriptors such as `ericw-light`, not only
  generic executable names like `light`.
- Setup diagnostics separate missing executable, missing source submodule,
  failed version probe, failed helper-tool smoke test, and likely dependency
  failure.
- `bspinfo`, `bsputil`, and `lightpreview` have first-class registry
  descriptors and readiness warnings; operation-level helper readiness probes
  remain planned.
- `lightpreview` remains optional; if launched later, VibeStudio documents
  platform-specific Qt/OpenGL risks and prefers isolated temp paths.

Issues mitigated by VibeStudio: #335, #435, #480, #482.

Still upstream-only: upstream package naming, `bsputil` parser fixes,
`lightpreview` platform launch fixes, and Embree/TBB build compatibility.

Tests:
- Implemented: profile/registry smoke tests cover descriptor-based discovery,
  helper descriptors, executable overrides, plan readiness, isolated temp
  environment, and known issue catalog entries for helper-tool risks.
- Planned: conflicting `light` ambiguity fixtures, `bsputil` operation probes,
  dependency-load diagnostics, and optional `lightpreview` launch smoke tests.

## Release Checklist

- Known-issue warnings cite upstream issue IDs and explain whether the local
  action is map data, profile options, compiler version, or upstream tracking.
- CLI output exposes categorized warnings; GUI parity for diagnostic code,
  severity, message, and suggested action remains a release requirement.
- AI-free compiler workflows remain complete; diagnostics must not require any
  cloud connector.
- Manifests preserve enough context for support, rerun, and regression triage
  without embedding secrets or private paths unnecessarily.
- Documentation in `docs/COMPILER_INTEGRATION.md` and this plan is updated when
  VibeStudio changes mitigation scope or when upstream ericw-tools resolves a
  referenced issue.
