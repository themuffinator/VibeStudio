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
- Project manifests can carry project-local overrides for selected install,
  editor profile, palette, compiler profile, compiler executable search
  paths/overrides, registered compiler outputs, and AI-free mode.

## Goals
- Detect installed games without modifying them.
- Let users confirm and edit detected profiles.
- Track base game paths, mod paths, source ports, compilers, palettes, package roots, launch commands, and test-map commands.
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
- Detection must be read-only.
- Profile creation must be user-confirmed.
- Package writes should happen through staged project/package workflows.
- Never assume commercial game data can be redistributed.
