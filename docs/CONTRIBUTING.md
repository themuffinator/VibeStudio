# Contributing

VibeStudio contributions should move the roadmap through small, credited,
testable vertical slices. Prefer one workflow becoming genuinely more usable
over a large isolated subsystem that cannot be validated from the GUI or CLI.

## Task Sizing

- Start from `docs/ROADMAP.md` and name the exact item or gap being addressed.
- Keep the first implementation slice small enough to build, test, document,
  and explain in one pull request.
- Add shared services before one-off GUI or CLI behavior when both surfaces will
  need the same data.
- Keep AI-assisted features reviewable and preserve an AI-free path.

## Validation Expectations

- Add or update Meson tests for shared C++ behavior.
- Add CLI validation when a workflow can run headless.
- Add fixture-backed tests for parsers, package readers/writers, map operations,
  compiler plans, and path handling.
- Run the closest focused check first, then the canonical build wrapper before
  requesting review.

Recommended local command on Windows:

```powershell
.\scripts\meson_build.ps1
```

Recommended local command on Unix-like systems:

```sh
meson setup builddir --backend ninja
meson compile -C builddir
meson test -C builddir --print-errorlogs
python scripts/validate_build.py --binary builddir/src/vibestudio --expected-version "$(cat VERSION)"
python scripts/validate_cli_docs.py --binary builddir/src/vibestudio
python scripts/validate_credits.py
python scripts/extract_translations.py --check --dry-run
python scripts/validate_samples.py --binary builddir/src/vibestudio
python scripts/validate_packaging.py --binary builddir/src/vibestudio
python scripts/validate_release_assets.py --binary builddir/src/vibestudio
```

## Accessibility And Localization

- Keep user-visible strings translatable.
- Prefer non-color-only status, accessible labels, keyboard paths, and summary
  plus detail surfaces.
- Consider text expansion, right-to-left layout, high-visibility themes, and
  app text scaling for new UI.
- Keep CLI output readable as plain text and available as JSON where automation
  needs it.

## Credits And Licensing

Update credits in the same change that imports, ports, translates, adapts, or
derives from another source.

- Update README Credits and `docs/CREDITS.md`.
- Preserve upstream license headers and copyright notices.
- Add nearby source comments for directly derived functions, layouts, or data
  structures.
- Update `docs/DEPENDENCIES.md` when adding or changing libraries.
- Update `docs/COMPILER_INTEGRATION.md` for external compiler/toolchain changes.
- Run `scripts/validate_credits.py` when credits or compiler submodule pins
  change; it compares README credits, `docs/CREDITS.md`, `.gitmodules`, and
  `src/core/studio_manifest.cpp`.

Borrowed items include code, docs, algorithms, file-format knowledge, UI
patterns, tests, assets, and public specifications.
