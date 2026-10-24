# Persistent mesh connectivity

Status: implementation accepted in the scoped Windows checks below. This is part of the modeller production
goal, not a replacement for its mesh, UV, animation or delivery requirements.

Legacy geometric adjacency groups indices whose positions match in all
poses. That reconnects coincident splits and can join independent duplicate
geometry. Optional per-index topology identities are now retained in editable mesh
version 11. Two corners may share geometric adjacency only when both their
identity and every pose position match. UV/normal copies keep identity;
intentional separation and duplication allocate distinct identities. Legacy
sources retain their existing inferred connectivity until materialised.

The identities are surface-local opaque integers. Remapping their numeric
labels must preserve their equivalence classes. Moving vertices does not
implicitly merge different identities. Explicit weld/merge operations may
unify them. Builders must share identities across UV/normal copies of the same
new point while keeping disconnected input regions separate.

Implemented and checked:

- Strict versioned source validation, save/reload, history, recovery, memory
  accounting, fingerprints and assembly compatibility.
- Geometric selection, normals and quad interpretation honour separation.
- Mesh construction, subdivision/cutting, bevel, spin, decimation, symmetry,
  UV splits/projection/packing and surface extraction/join preserve identities.
- Rip can retain a zero-distance open split; cancelling a preview still
  cancels the whole operation. Filling a zero-width gap must avoid invalid
  faces and expose an actionable refusal.
- Actual GUI/CLI parity, worker cancellation, F9, deterministic results,
  native derivative validation and documentation/localisation.
- Existing legacy fixtures and topology workflows continue to pass.

Native model formats do not generally encode this authoring metadata. Keep
the editable source authoritative; document native reimport inference and
avoid promising lossless source editing through a game-format round trip.

Skeletal reconciliation no longer chooses nearest source vertices. Every edit
records where each vertex came from while it runs (`core/model_vertex_origins`),
and influences are copied, interpolated, mirrored, merged or bound from that
record; a skinned surface without one is refused. Coincident vertices with
independently authored weights keep them through every edit kind
([skin weights through mesh edits](../MODEL_SKIN_PROVENANCE.md)). Merges are an
equal blend and position tools still make the frames they edit independent; the
full modeller production goal remains open.

## Evidence

The final run passed 60 unique suites plus three GUI reruns at 2x device pixels
(63 executions), with 494 scoped source/build inputs and 61 executables stable
through acceptance. The identity suite passed 416 checks; Rip passed 133
core/CLI checks and 183 GUI checks at each scale. Assembly animation passed 63
checks including explicit-source baking. Twenty-four owned Rip renders cover
open, filled, zero-distance and F9 states; four final images were reviewed.
The surrounding editor remains crowded at 200% expanded text.

Evidence and reproducible scripts are under
`.agents/tmp/modeller-topology-identity/`, with the acceptance record in
`evidence/milestone.json`. Documentation, CLI and 48 catalogue checks passed.
The unrelated source-layout finding for `core/level_textured_selection.h`
remains. Original-engine, real-project, physical accessibility and clean
macOS/Linux acceptance are still required for the full production goal.
