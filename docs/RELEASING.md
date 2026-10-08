# Releasing VibeStudio

Two files drive every release: **`VERSION`** says what is being released, and
**`CHANGELOG.md`** says what changed. The [release workflow](../.github/workflows/release.yml)
turns them into tested downloads for every platform and a GitHub release.

```text
 changes land ──► CHANGELOG.md [Unreleased] ──► version.py + changelog.py release ──► tag v<VERSION>
                                                                                          │
   GitHub release ◄── notes + SHA256SUMS ◄── Windows · macOS · Linux · docs · source ◄────┘
```

## Versions

VibeStudio uses [Semantic Versioning 2.0](https://semver.org/spec/v2.0.0.html).
`VERSION` holds exactly one version; Meson reads it, the app reports it, and
`scripts/release_meta.py` derives every name from it.

| Label | Meaning | Example |
| --- | --- | --- |
| `-alpha.N` | Feature work in progress; expect gaps and breakage | `0.1.0-alpha.1` |
| `-beta.N` | Feature-complete for the milestone; testing and fixes | `0.2.0-beta.1` |
| `-rc.N` | A candidate we would ship unless testing finds a problem | `0.2.0-rc.1` |
| none | A release | `0.2.0` |

While the major version is 0, a minor bump (`0.1` to `0.2`) may change
behaviour or formats; a patch bump only fixes. Builds that are not releases
(nightlies and workflow dry runs) are labelled `<VERSION>+<date>.<commit>`;
build metadata is never stored in `VERSION`.

`scripts/version.py` changes the version and refreshes everything that quotes
it (the README badge and the generated offline guide):

```sh
python scripts/version.py                         # show the version, tag and download names
python scripts/version.py bump pre                # 0.1.0-alpha.1 -> 0.1.0-alpha.2
python scripts/version.py bump pre --label beta   # 0.1.0-alpha.2 -> 0.1.0-beta.1
python scripts/version.py bump release            # 0.1.0-rc.2    -> 0.1.0
python scripts/version.py bump minor --pre alpha  # 0.1.0         -> 0.2.0-alpha.1
python scripts/version.py set 0.3.0-beta.1
```

## The changelog

Every change a user could notice adds one line under `## [Unreleased]`, in
the same pull request. Use the [Keep a Changelog](https://keepachangelog.com/en/1.1.0/)
sections (Added, Changed, Deprecated, Removed, Fixed, Security), start with the
studio area in bold, and write for users:

```sh
python scripts/changelog.py add fixed "**Packages:** Extracting a PK3 keeps file dates."
python scripts/changelog.py show      # what the next release will say
python scripts/changelog.py check     # structure, order and links (also run in CI)
```

Internal refactors, test changes and CI tweaks don't need an entry unless they
change what users get.

## Cut a release

1. **Pick the version.** `python scripts/version.py bump ...` (or `set`).
2. **Close the changelog.** `python scripts/changelog.py release` moves the
   Unreleased entries into `## [<VERSION>] - <today>` and updates the compare
   links. Read the result as a user would and tidy it.
3. **Optional: curated notes.** For a release that needs more than the
   changelog (a first preview, a big migration), write
   `docs/releases/<VERSION>.md`. The workflow publishes it instead of the
   changelog section; start with `## Highlights`.
4. **Check locally.**

   ```sh
   python scripts/changelog.py check --release "$(cat VERSION)"
   python scripts/sync_doc_versions.py --check
   python scripts/build_docs_site.py
   ```

5. **Merge** the release pull request to `main`.
6. **Publish**, either way:
   - push a tag: `git tag v<VERSION> && git push origin v<VERSION>`, or
   - run **Actions** > **release** > **Run workflow** on `main` with
     **publish** ticked; the workflow creates the tag on that commit. Tick
     **draft** to review the release before it goes public.
7. **Start the next cycle.** Bump `VERSION` to the next pre-release
   (`python scripts/version.py bump pre`) so nightly builds carry the upcoming
   version.

## What the workflow does

| Job | Builds | Checks before upload |
| --- | --- | --- |
| Prepare | Resolves the version, tag and channel | Tag matches `VERSION`; the tag is new; `CHANGELOG.md` has the version; branding and README are current |
| HTML documentation | The offline manual, `VibeStudio-<v>-docs.zip` | Every page link and anchor resolves |
| Windows (x64) | Verified build with source evidence, portable ZIP, runtime source ZIP, Inno Setup installer | Meson tests, CLI/packaging/release validators, packaged runtime smoke test, silent install and launch |
| macOS (Apple silicon) | `VibeStudio.app` (macdeployqt, ad-hoc signed) in a branded DMG | Meson tests, validators, bundle launch, `hdiutil verify` |
| Linux (x86_64) | AppImage via linuxdeploy, built on Ubuntu 22.04 | Meson tests, validators, desktop entry and AppStream checks, AppImage launch |
| Source archive | `VibeStudio-<v>-source.tar.gz` with compiler submodules | — |
| Publish | Renames everything to the published names, writes `SHA256SUMS.txt`, `release-manifest.json` and the notes, creates the GitHub release | Runs only when publishing and every job passed |

Without **publish**, the workflow is a full dry run: every download is kept as
a workflow artifact for 14 days. Pre-release versions are marked as
pre-releases on GitHub. The release notes add downloads, a status warning,
checksum instructions, build details and the commit list to the changelog
section.

The scheduled `release-nightly` workflow keeps building development artifacts
as before; it never publishes a release.

## Known gaps

- **No code signing yet.** Windows SmartScreen and macOS Gatekeeper warn on
  first launch; [Install VibeStudio](manual/install.md) explains the steps.
  Signing and notarisation need certificates the project does not have yet.
- **macOS builds are Apple silicon only**, and **Linux builds are x86_64
  only**, without in-studio audio playback or recording.
- **linuxdeploy is fetched from its continuous release.** The workflow logs
  its SHA-256 for every run; pin it once upstream publishes stable releases.
- **Hosted runs of this workflow have not been exercised end to end yet.** The
  first release should start as a dry run, then a draft.

## Run the packaging steps locally

| Platform | Command |
| --- | --- |
| Any | `python scripts/build_docs_site.py --output build/docs-site` |
| Windows | `python scripts/package_windows_release.py ... --docs-site build/docs-site`, then `python scripts/package_windows_installer.py --package-dir <staged package>` (needs Inno Setup 6) |
| macOS | `python scripts/package_macos_app.py --binary builddir/src/vibestudio --compiled-translations builddir/i18n --docs-site build/docs-site` |
| Linux | `meson setup builddir --prefix=/usr`, build, then `python scripts/package_appimage.py --build-dir builddir --linuxdeploy <path>` |
| Any | `python scripts/package_source_tarball.py` |

See [Packaging](PACKAGING.md) for the portable package layout and the Windows
runtime evidence, and [Branding](BRANDING.md) for the installer art.
