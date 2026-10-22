# The game profile tables the map builder, entity placer and validator read, compiled from
# data/profiles at build time (generate_profile_tables.py, a port of Quake-MapGen's CMake step).
# Built here so the header lands in the build tree as qmg/generated/game_profile_tables.hpp.
mapgen_profile_tables = custom_target('mapgen_profile_tables',
  input: [files('../../../generate_profile_tables.py')] + mapgen_profile_files,
  output: 'game_profile_tables.hpp',
  command: [mapgen_python, '@INPUT0@', mapgen_root, '@OUTPUT@'],
)
