# Game Installations

VibeStudio will build on PakFu's installation profile concept and expand it
into project-aware game management.

## Goals
- Detect installed games without modifying them.
- Let users confirm and edit detected profiles.
- Track base game paths, mod paths, source ports, compilers, palettes, package roots, launch commands, and test-map commands.
- Support multiple installs of the same game.
- Keep project-local overrides separate from global user profiles.

## Detection Sources
Initial detection should cover:
- Steam libraries.
- GOG installations.
- Epic/EOS installations where relevant.
- Manually selected directories.
- Common source-port and mod-manager layouts.
- Existing PakFu profile data once a migration path exists.

## Profile Data
Each profile should eventually include:
- Stable profile ID.
- Game key and engine family.
- Display name.
- Installation root.
- Base package directories.
- Mod/package directories.
- Executable path and launch arguments.
- Palette and texture defaults.
- Compiler profile defaults.
- Validation rules for required files.

## Safety Rules
- Detection must be read-only.
- Profile creation must be user-confirmed.
- Package writes should happen through staged project/package workflows.
- Never assume commercial game data can be redistributed.
