# Game Installations

VibeStudio builds on PakFu's installation profile concept and expands it into
project-aware game management.

## Current Implementation

- Manual profiles are active in settings, GUI, and CLI.
- Steam and GOG detection are active as read-only candidate scans. Detected
  candidates are shown for confirmation/import in the GUI and through
  `--detect-installations` or `install detect`; candidates are not saved until
  the user confirms them.
- Profiles store a stable ID, game key, engine family, display name,
  installation root, optional executable, base/mod package paths, palette
  default, compiler-profile default, read-only state, hidden state, and
  timestamps.
- Known game keys currently include `custom`, `doom`, `heretic-hexen`,
  `quake`, `quake2`, and `quake3`.
- Validation is read-only: it checks the root path, optional executable, saved
  package paths, expected base package hints, and whether the engine family is
  still generic.
- CLI commands are available through `--installations-report`,
  `--detect-installations`, `--add-installation`, `--select-installation`,
  `--validate-installation`, `--remove-installation`, `install list`, and
  `install detect`.
- The read-only state is enforced for test maps. A Quake-family engine loads a
  map only from a game folder's `maps` directory, so launching a build copies
  it there (`<root>/<game folder>/maps`, plus any `.lit` and `.lux` beside the
  BSP). That copy is refused while the profile is read-only, which it is by
  default; the Build page asks **Allow Test Maps** once and saves the answer on
  the profile, and `launch run --deploy --allow-test-maps` does the same from
  the CLI. The Workspace installation list marks such profiles "test maps
  allowed" and has an **Allow test maps** check box for the selected profile,
  which also takes the permission back. The game folder is a single folder name inside the installation,
  never a path, and nothing else in the installation is written.
- Project manifests can carry project-local overrides for selected install,
  editor profile, palette, compiler profile, compiler executable search
  paths/overrides, registered compiler outputs, and AI-free mode.

## Goals
- Detect installed games without modifying them.
- Let users confirm and edit detected profiles.
- Track base game paths, mod paths, source ports, compilers, palettes, package roots, launch commands, and test-map commands. [test-map copy active: each known game declares its base game folder]
- Support multiple installs of the same game.
- Keep project-local overrides separate from global user profiles.

## Detection Sources
Initial detection should cover:
- Steam libraries. [active]
- GOG installations. [active]
- Epic/EOS installations where relevant.
- Manually selected directories. [active]
- Common source-port and mod-manager layouts.
- Existing PakFu profile data once a migration path exists.

## Profile Data
Each profile should eventually include:
- Stable profile ID. [active]
- Game key and engine family. [active]
- Display name. [active]
- Installation root. [active]
- Base package directories. [active]
- Mod/package directories. [active]
- Executable path and launch arguments. [executable path active; launch
  arguments planned]
- Palette and texture defaults. [palette default active; texture defaults
  planned]
- Compiler profile defaults. [active]
- Validation rules for required files. [active]
- Project-local selected-install overrides. [active]

## Safety Rules
Prepared Quake-family builds deploy the complete captured assets and runtime
outputs through the ordinary package publisher. Quake/Quake II use reviewed
numbered PAK slots; Quake III uses `vibestudio_<map>.pk3`. A per-slot receipt
remembers each classic map without authorizing replacement of registered base
or mod packages. Read-only permission is granted per operation and never saved
as a profile change. Review details, backup behavior and source-port limitations
are documented in [prepared builds](LEVEL_EDITOR.md#prepared-builds-with-current-assets).

- Detection must be read-only.
- Profile creation must be user-confirmed.
- Package writes should happen through staged project/package workflows.
- Never assume commercial game data can be redistributed.
