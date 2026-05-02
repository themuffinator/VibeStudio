# VibeStudio Samples

These sample projects are intentionally tiny, license-clean workspaces for CI
and manual smoke testing. They contain placeholder text assets only; no
commercial game data, SDK assets, or proprietary content is included.

Each sample includes:
- `.vibestudio/project.json` for the project manifest service.
- `maps/` with a tiny placeholder map or build input.
- `packages/` with folder-package content for read-only package browsing.
- `out/` and `.vibestudio/cache/` folders so health checks can exercise output
  and temporary paths without creating files.

Run the smoke checks with a built VibeStudio CLI:

```sh
python scripts/validate_samples.py --binary builddir/src/vibestudio
```
