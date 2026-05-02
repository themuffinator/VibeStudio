# ericw-tools Remaining Bugs Resolution Matrix

Date: 2026-05-02

Context snapshot: `.omx/context/ericw-remaining-bugs-20260502T094709Z.md`

Source audit: `docs/plans/ericw-tools-open-issues-audit.md`

This matrix records the Ralph remaining-bugs pass for the 62 ericw-tools issue
IDs that were outside the first high-value acceptance set. It does not claim
that ericw-tools compiler algorithms, BSP output, lighting output, VIS output,
or helper-tool argument parsing are fixed upstream. VibeStudio's ownership is
to warn, preflight, validate artifacts where local services can inspect them,
and keep upstream limitations visible.

## Status Definitions

| Status | Acceptance meaning |
|---|---|
| Implemented catalog warning | `src/core/compiler_known_issues.*` has an issue descriptor with upstream URL, severity, tool/profile scope, match keywords, warning text, and local action text. |
| Implemented map preflight warning | `src/core/ericw_map_preflight.*` emits a map/entity/path warning before the compiler starts. These IDs also remain in the catalog for help text and matching. |
| Implemented registry/helper readiness | Registry or setup diagnostics make source/executable/probe/helper readiness visible before users rely on the tool. This pass adds first-class `bspinfo`, `bsputil`, and `lightpreview` discovery plus readiness warnings; deeper helper-operation probes remain a separate future wrapper task. |
| Implemented artifact validation | VibeStudio validates produced artifacts before promotion, packaging, or release. This pass adds lightweight expected-output, BSP-family, lump, face-reference, conversion-output, and BSPX metadata checks; catalog warnings remain the honest mitigation for output features the validator cannot inspect. |
| Upstream-only tracked limitation | VibeStudio can document, warn, or create regression fixtures, but the functional fix belongs in ericw-tools or a documented fork/submodule update. |

## Acceptance Matrix

Each issue ID appears in exactly one row in this table.

| Status | Issue IDs | Count | Acceptance note |
|---|---:|---:|---|
| Implemented map preflight warning | #455, #443, #342, #327, #291, #268, #262, #256, #255, #254, #247, #241, #238, #233, #217, #209, #190, #172, #140, #136, #129, #121 | 22 | Preflight catches the source-map, entity, brush, bmodel, texture, prefab, and selected lighting-risk shapes before invoking ericw-tools. |
| Implemented catalog warning | #437, #411, #401, #374, #351, #298, #297, #214, #203, #195, #186, #185, #145, #134, #109, #106, #105, #91, #81 | 19 | Catalog descriptors provide scoped warnings and local actions where VibeStudio cannot safely infer a specific source-map violation or cannot inspect the requested output feature directly. |
| Implemented registry/helper readiness | #480, #449, #335, #225, #223, #215 | 6 | Registry descriptors and setup diagnostics make generic binary names, helper availability, captured output, packaging names, and optional `lightpreview` launch risk visible. Operation-level helper behavior remains upstream-owned unless a smoke-tested wrapper operation exists. |
| Implemented artifact validation | #415, #309, #249, #213 | 4 | VibeStudio now preserves manifests, inspects generated BSP artifacts, and keeps conversion output paths plus BSPX/lightmap metadata expectations explicit. Compiler-side output generation remains upstream-owned, and catalog warnings cover decompile/export features the validator cannot inspect. |
| Upstream-only tracked limitation | #436, #425, #413, #285, #281, #277, #246, #234, #222, #200, #189 | 11 | These are compiler features, lighting/material controls, performance changes, or internal cleanup requests. VibeStudio tracks them and may expose options only after upstream support exists or a documented fork is approved. |

Total: 62 distinct issue IDs.

## Boundary Notes

- Map preflight warnings mitigate risky inputs; they do not prove the compiler
  will emit correct geometry, lighting, VIS data, or BSPX lumps.
- Catalog warnings are product-owned explanation and triage. They are not
  upstream fixes.
- Registry/helper readiness is limited to discoverability, probe results, and
  operation smoke checks. Native `bsputil`, `bspinfo`, and `lightpreview`
  behavior remains upstream-owned.
- Artifact validation can reject, quarantine, or warn about outputs before
  packaging. It does not repair a bad BSP or replace ericw-tools algorithmic
  fixes.
- New compiler features should be exposed in VibeStudio only after upstream
  support exists or after a documented VibeStudio fork is approved.
