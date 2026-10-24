# Place hands and feet with inverse kinematics

**Available:** two-bone inverse kinematics (IK) over retained skeletal clip
samples, with a model-space target and bend pole. VibeStudio remains pre-alpha
and largely untested in real projects. This does not establish the
[production gate](MODELLER_RELEASE.md).

## Review a pose

Open **Mesh Editor → Skeleton → Two-Bone Inverse Kinematics**. Choose a clip
and an **End joint**. Its parent and grandparent form the lower and upper bone;
the resolved chain appears beneath the controls. Each bone must have a nonzero
length in every affected sample. The grandparent's origin stays fixed.

1. Choose **First sample** and **Last sample**, inclusive and zero-based within
   the clip. A range can keep a foot planted while the root moves.
2. Set **Target X/Y/Z** to the desired end position in model units.
3. Set **Pole X/Y/Z** to a point on the desired elbow or knee side. The pole
   chooses the bend plane; it is not an extra joint. Keep it off the line from
   the root to the target whenever the solution bends.
4. Choose **Review**, compare **Reviewed result** and **Original model** in
   **Preview**, and inspect every affected sample in **Results**.
5. **Apply** commits one undoable edit. Changing a setting requires another
   review. **Cancel** discards the candidate and cancels an active worker.

**Use First Sample Pose** fills the target from the current end position and
the pole from the current bend. A straight chain uses a perpendicular model
axis; adjust it to match the intended elbow or knee direction. The target and
pole stay constant across the selected range. The GUI and the CLI both solve
any clip with poses, including clips outside the
[working set](MODEL_SKELETAL_WORKING_SET.md); the preview skins those one sample
at a time.

## Choose reach and orientation policies

**Require target** refuses the entire operation if any sample is unreachable.
**Nearest reachable** explicitly clamps the end to the chain's reach. Neither
policy stretches bones. **Results** reports the remaining target distance and
whether the target was reached, including unchanged but unreachable poses.
For bone lengths `a` and `b`, reach is between `abs(a-b)` and `a+b`.

**Keep end orientation** preserves the hand or foot's original model-space
basis. **Follow lower bone** rotates it with the lower bone. Children of the
end follow this choice. Other branches follow their nearest chain ancestor.
The solver applies rigid swings to the existing affine bases, retaining scale
and shear rather than reconstructing poses from decomposed rotations.

At a fully folded equal-length chain, the pole selects the middle joint's
direction from the root. At an unreachable target exactly on the root, clamping
uses the previous root-to-end direction, or the upper bone direction if folded.
Opposite directions use a deterministic half-turn axis. There are no implicit
joint-angle limits, stretch, twist controls or persistent IK constraints.

The positional tolerance is `max(0.00001, (a+b)*0.000002)` model units. Coordinates
must be finite and within ±1,000,000. If float storage cannot preserve bone
lengths within that tolerance, the edit fails. Reapplying an already solved
constraint within tolerance leaves the pose and undo history unchanged.

## Preserve the authoring workflow

IK writes ordinary dense skeletal samples. The bind pose and weights stay
unchanged. The shared skeletal rebuild updates skinned geometry and linked
attachments only where their poses change. Independently edited affected
geometry, normals or attachment poses refuse the edit; unaffected independent
edits survive. Frame names/origins, custom animation ranges, manual tags,
collision tracks, material bindings and UVs remain intact. A changed clip's
cached bounds are invalidated. Save, undo, redo and recovery use the normal
document services and existing editable source format.

If an affected joint has [stored curves](MODEL_ANIMATION_CURVES.md), a solve
that would invalidate those curves is refused. Bake the affected joint curves
explicitly before using IK; sampled motion remains and undo restores the keys.

Export through the existing MD5/IQM or baked MDL/MD2/MD3 path and its format
checks. Refit collision explicitly after posing, refresh derivatives used by
level placement and packaging, and review game animation scripts and playback.
There is no automatic package publication, map compilation or engine launch.
Native round-trip fixtures do not prove acceptance in the original engines.

## Automate the solve

```sh
vibestudio --cli model ik ./models/player.mesh.json --clip 0 --first 0 --last 8 --end-joint foot.L --target 0,8,0 --pole 0,4,8 --output ./models/planted.mesh.json --dry-run --json
```

Input is positional or supplied with `--input`. All of `--clip`, `--first`,
`--last`, `--end-joint`, `--target`, `--pole` and `--output .mesh.json` are
required. Joint names match exactly. Optional flags are `--nearest-reachable`,
`--follow-end`, `--dry-run`, `--overwrite` and normal CLI output/localisation
options. Repeated, unknown or malformed options are refused.

JSON includes per-sample end position, target distance, tolerance and `reached`,
plus `dryRun` and `written`. A clamped result is successful but can have
`reached: false`; automation should inspect it. Dry-run validates the source,
solve and destination without writing. Existing output requires `--overwrite`.
The CLI and GUI apply the same `SolveTwoBoneIk` document edit.

## Remaining animation work

Long-chain IK, persistent constraints, joint limits,
retargeting and target/pole viewport handles remain planned. Physical
keyboard/screen-reader acceptance, real-project animation quality, maximum-size
performance, engine playback and cross-platform release checks remain open.
The numeric controls, native results table and full detail field provide
keyboard-accessible equivalents for the current workflow. No new dependency,
cloud service or AI connector is required.
