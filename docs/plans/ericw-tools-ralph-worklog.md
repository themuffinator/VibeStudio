# ericw-tools Ralph Agent Worklog

Date: 2026-05-02

Parent task: `docs/plans/ericw-tools-high-value-resolution.md`

Audit source: `docs/plans/ericw-tools-open-issues-audit.md`

This records the five-subagent Ralph loop used to split the ericw-tools
high-value mitigation work. Each lane had a concrete implementation or review
scope and returned either file changes or blocking review findings.

## Agent Lanes

| Agent | Subagent id | Lane | First pass result | Second pass result |
|---|---|---|---|---|
| Tesla | `019de7f8-e768-7ab2-861f-e3245eb0f9f9` | Known-issue catalog | Implemented `src/core/compiler_known_issues.*` plus smoke coverage for upstream issue metadata, dedupe clusters, matching, and formatted warning text. | Found missing high-value issue `#114`, added it to the catalog, deduplicated emitted issue warnings, and verified all 52 high-value audit rows had catalog coverage. |
| Noether | `019de7f8-e907-72d1-aae0-d0cbe4fe5cc3` | Quake `.map` preflight | Implemented `src/core/ericw_map_preflight.*` plus smoke coverage for absolute WADs, long values, backslashes, external-map hazards, grouped/toggled light conflicts, origins, and region risks. | Patched scanner gaps for `#135`, `#201`, `#207`, `#230`, `#231`, `#257`, `#417`, and tightened variants for `#199`, `#470`, and `#485`. |
| Jason | `019de7f8-ea27-7a80-a77d-8d630f365eb4` | Runner and manifest behavior | Patched compiler runner safety: missing working-directory refusal, isolated `TMP`/`TEMP`/`TMPDIR`, warning callbacks, rerun provenance warnings, and diagnostic path handling. | Fixed warning categorization so generic plan warnings, known-issue warnings, and preflight warnings are surfaced separately and dry-runs with ericw warnings report warning state. |
| Franklin | `019de7f8-ec70-7053-9547-41c86b4e1e54` | Documentation and scope boundary | Updated `docs/COMPILER_INTEGRATION.md` and added the high-value resolution plan, keeping VibeStudio mitigations separate from upstream compiler fixes. | Tightened overclaims about version/help probe manifest data, `bspinfo`/`bsputil` probes, version-keyed warnings, and remaining planned checks. |
| Curie | `019de7f8-eded-71b0-b5a0-aadc7120c6e2` | Architect verification | Rejected initial sign-off until docs distinguished wrapper/preflight mitigations from upstream compiler fixes and removed unsupported version/probe claims. | Rejected second sign-off only because the five-agent work evidence was not yet durable in the workspace; this worklog is the resulting artifact. |

## Verification Returned By Lanes

- Known-issue lane: `git diff --check` on catalog files, high-row audit
  comparison (`highRows=52`, no missing issue IDs), direct compile/link/run of
  the smoke test, and focused Meson tests for known issues and compiler
  profiles.
- Preflight lane: focused compile/run of the preflight smoke test, focused
  Meson preflight test, and full Meson test suite.
- Runner lane: focused compiler profile test build, focused Meson compiler
  profile smoke test, and `git diff --check` for runner/profile test files.
- Documentation lane: `git diff --check` for the three ericw docs and pattern
  checks for the corrected overclaim terms.
- Parent verification: full `meson test -C builddir --print-errorlogs`,
  build validation, sample validation, packaging validation, and release asset
  validation under `meson devenv`.

## Scope Notes

- This work resolves high-value bugs within VibeStudio's ownership boundary:
  wrapper safety, preflight diagnostics, known-issue warnings, manifests,
  CLI JSON, and documentation.
- It does not claim upstream ericw-tools compiler correctness fixes for BSP,
  VIS, lighting, BSPX, or helper-tool parser behavior.
- Remaining planned work is tracked in
  `docs/plans/ericw-tools-high-value-resolution.md`.

## Remaining-Bugs Ralph Pass

Pass date: 2026-05-02

Context snapshot:
`.omx/context/ericw-remaining-bugs-20260502T094709Z.md`

Acceptance matrix:
`docs/plans/ericw-tools-remaining-bugs-resolution.md`

This second Ralph pass covers the 62 remaining-pass ericw-tools issue IDs now
tracked by VibeStudio's mitigation layer. The pass keeps the same boundary as
the high-value work: VibeStudio mitigates, warns, validates, and records
provenance; upstream ericw-tools still owns compiler algorithm and helper-tool
behavior fixes.

### Remaining-Pass Lanes

| Lane | Scope | Current acceptance criteria |
|---|---|---|
| Lane 1 | Known-issue catalog warning coverage | The 62 remaining-pass issue IDs have catalog descriptors with upstream URLs, severities, tool/profile scope, keywords, warning text, and local action text. Verification should prove the catalog set is non-duplicative and matches the acceptance matrix. |
| Lane 2 | Map preflight warnings | Source-map, entity, path, region, prefab, and selected lighting hazards are emitted before invoking ericw-tools. Verification should cover representative fixtures and preserve upstream issue IDs in warning text. |
| Lane 3 | Registry and helper readiness | Core compiler registry readiness exposes source availability, executable discovery, overrides, capability flags, version/help probe results, and first-class `bspinfo`, `bsputil`, and `lightpreview` descriptors with readiness warnings. Operation-level helper probes remain future wrapper work. |
| Lane 4 | Artifact validation | Post-run gates validate expected outputs before registration, with lightweight BSP/BSPX checks for missing outputs, corrupt headers, wrong BSP family, truncated lumps, face-reference risks, conversion-output mismatches, and missing profile-requested metadata. These are artifact gates, not upstream compiler fixes. |
| Lane 5 | Docs, worklog, and acceptance matrix | Keep the audit, high-value plan, remaining-bugs matrix, worklog, and compiler integration docs aligned without adding duplicate audit issue rows or claiming upstream fixes. Verification should include Markdown issue-count checks and `git diff --check` on the edited docs. |

### Remaining-Pass Scope Notes

- The acceptance matrix groups issue IDs by VibeStudio-owned resolution status
  instead of duplicating the 125-row upstream audit table.
- Catalog and preflight warnings are user-facing mitigations; they do not
  guarantee output parity or visual correctness.
- Registry/helper readiness explains whether tools can be found or probed; it
  does not fix native `bsputil`, `bspinfo`, `lightpreview`, or build-system
  behavior.
- Artifact validation may block promotion or packaging of suspicious outputs,
  but ericw-tools remains responsible for producing correct BSP, VIS, lighting,
  and BSPX data.
