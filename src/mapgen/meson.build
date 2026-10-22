# VibeStudio Map Generator: the Quake-MapGen prompt-to-blockout pipeline for Quake, Quake II and
# Quake III Arena, imported from https://github.com/themuffinator/Quake-MapGen (MIT) and developed
# here from now on. Plain C++20 with no Qt: core/level_mapgen bridges it into the studio. See
# VIBESTUDIO.md for its provenance and what VibeStudio changed.
mapgen_python = find_program('python', 'python3')
mapgen_root = meson.current_source_dir()
mapgen_profile_files = files(
  'data/profiles/schema.json',
  'data/profiles/q1/generator-current.json',
  'data/profiles/q1/id1-1996.json',
  'data/profiles/q1/rerelease-2021.json',
  'data/profiles/q1/threewave-5.0.json',
  'data/profiles/q2/baseq2-3.19.json',
  'data/profiles/q2/ctf-3.19.json',
  'data/profiles/q2/generator-current.json',
  'data/profiles/q3/baseq3-1.32b.json',
  'data/profiles/q3/generator-current.json',
)
subdir('include/qmg/generated')

mapgen_sources = files(
  'src/construction.cpp',
  'src/entities.cpp',
  'src/generator.cpp',
  'src/layout_fingerprint.cpp',
  'src/layout_ranker.cpp',
  'src/leak.cpp',
  'src/lighting.cpp',
  'src/map_builder.cpp',
  'src/map_document.cpp',
  'src/map_document_writer.cpp',
  'src/map_parser.cpp',
  'src/map_writer.cpp',
  'src/mp_motifs.cpp',
  'src/navigation.cpp',
  'src/pipeline.cpp',
  'src/prompt.cpp',
  'src/quality.cpp',
  'src/repair.cpp',
  'src/report.cpp',
  'src/semantic_plan.cpp',
  'src/semantic_planner.cpp',
  'src/sha256.cpp',
  'src/spawn_refinement.cpp',
  'src/switches.cpp',
  'src/types.cpp',
  'src/upper_links.cpp',
  'src/validator.cpp',
  'src/visibility.cpp',
)

# The generator's own version and the llama.cpp revision its report records: the pinned one in
# builds with -Dggml=enabled, else "not-linked".
mapgen_version = '0.4.0+vibestudio'
mapgen_args = [
  '-DQMG_VERSION="' + mapgen_version + '"',
  '-DQMG_LLAMA_REVISION="' + (ggml_revision != '' ? ggml_revision : 'not-linked') + '"',
]
# With -Dggml=enabled the ranker can also run its upstream ggml graph (qmg::RankerBackend::ggml).
if ggml_dep.found()
  mapgen_args += ['-DQMG_HAVE_GGML=1']
endif
mapgen_includes = include_directories('include')

vibestudio_mapgen = static_library(
  'vibestudio_mapgen',
  mapgen_sources, mapgen_profile_tables,
  include_directories: mapgen_includes,
  cpp_args: mapgen_args,
  dependencies: ggml_dep,
  install: false,
)

mapgen_dep = declare_dependency(
  include_directories: mapgen_includes,
  compile_args: mapgen_args,
  link_with: vibestudio_mapgen,
  dependencies: ggml_dep,
  sources: mapgen_profile_tables,
)

install_data('LICENSE', 'VIBESTUDIO.md', install_dir: get_option('datadir') / 'vibestudio/licenses/quake-mapgen')

# Quake-MapGen's own unit tests, unchanged apart from the ranker backend they expect. Timeouts allow
# for debug builds, where the slowest take a minute or more.
mapgen_tests = {
  'main': 600,
  'construction': 120,
  'leak': 120,
  'lighting': 120,
  'map-builder': 120,
  'map-document': 120,
  'mp-motifs': 600,
  'navigation': 120,
  'opposed-bridge': 120,
  'quality': 120,
  'ranker-diagnostics': 120,
  'spawn-refinement': 120,
  'switches': 120,
  'upper-links': 300,
  'visibility': 120,
  'wrapped-intent': 180,
  'r1s-intermission': 180,
  'r1s-monster-hull-support': 600,
  'r1s-mp-player-start': 600,
  'r1s-q3-ctf-team-begin': 600,
  'r1s-sp-coop-starts': 300,
  'r1s-sp-pickup-hull-support': 600,
}
foreach name, timeout : mapgen_tests
  mapgen_test = executable('mapgen_' + name.underscorify() + '_test',
    'tests/test_' + name.underscorify() + '.cpp',
    dependencies: mapgen_dep,
    install: false,
  )
  test('mapgen-' + name, mapgen_test, timeout: timeout, suite: 'mapgen')
endforeach
subdir('tools')
