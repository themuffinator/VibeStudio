# Packaging

VibeStudio's MVP packaging support creates portable release directories and zip
archives for Windows, macOS, and Linux targets. These packages are suitable for
release-candidate validation and handoff, but they are not yet signed,
notarized, installer-backed, or Qt-deployed production artifacts.

## Portable Package Script

`scripts/package_portable.py` stages a versioned package directory containing:

- the built `vibestudio` executable under `bin/`;
- `README.md`, `VERSION`, repository documentation, and the generated
  `docs/OFFLINE_USER_GUIDE.md`;
- `samples/` with license-clean Doom, Quake, and Quake III-family projects;
- `licenses/THIRD_PARTY_LICENSES.md` plus VibeStudio license/credits files and
  imported compiler source licenses for ericw-tools, q3map2-nrc, ZDBSP, and
  ZokumBSP;
- `platform/README.txt` with target-specific launch and clean-machine smoke
  notes;
- `package-manifest.json` schema version 2 with target platform,
  architecture, host platform, binary checksum, docs, samples, license bundle,
  Qt deployment notes, and compiler-bundling status;
- `CHECKSUMS.sha256` covering staged release files.

Create the current-platform package:

```sh
python scripts/package_portable.py --binary builddir/src/vibestudio --archive
```

Create all target-shaped package directories from the available binary:

```sh
python scripts/package_portable.py --binary builddir/src/vibestudio --archive --target-platform all
```

On Windows, use `builddir/src/vibestudio.exe` for the binary path.

## Validation

Regenerate the offline guide:

```sh
python scripts/generate_offline_guide.py
```

Check that the committed offline guide is current:

```sh
python scripts/generate_offline_guide.py --check
```

Smoke-test current-platform packaging:

```sh
python scripts/validate_packaging.py --binary builddir/src/vibestudio
```

Run the MVP release asset gate:

```sh
python scripts/validate_release_assets.py --binary builddir/src/vibestudio
```

The release asset validator checks all three target package manifests, zip
archives, offline guide freshness, license bundles, checksums, sample/package
fixtures for folder/PAK/WAD/PK3, visible-feedback JSON for package extraction,
compiler task-state JSON, AI workflow JSON, startup timing, package-open timing,
and static release UX coverage signals.

## Remaining Production Release Work

- Run platform Qt deployment tools (`windeployqt`, `macdeployqt`, or a Linux
  deployment/AppImage/distribution flow) before publishing binary artifacts.
- Add signing, notarization, published checksums, release notes, and long-term
  artifact retention.
- Decide whether external compiler executables are downloaded, bundled, or kept
  separate per release channel.
- Add package update metadata after release channels are defined.
