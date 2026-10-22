# Quake-MapGen's research tooling (VIBESTUDIO.md, "Research tools"). The map recipes and the
# multiplayer portfolio programs build against mapgen_dep and are never installed. The tests mirror
# Quake-MapGen's CMakeLists.txt and cmake/Qmg*.cmake for everything that runs without game data,
# compilers, engines or Node, all in the mapgen-research suite. The native movement and teleport
# harnesses (native_movement/harness.c, native_teleport/*.c) link GPL Quake II code from outside this
# tree: they are kept as source only and are never built here.

# Explicit design experiments, built from the same builder and analysis contracts as the generator.
# Each writes its layout into the build directory as qmg_<recipe>_layout did; debug builds are slower
# than the 60 seconds Quake-MapGen allows its release builds.
mapgen_recipes = [
  'switchback',
  'spillway',
  'counterweight',
  'talus',
  'talus_stairs',
  'undercut',
  'fold',
  'lockyard',
  'ravel',
  'relay',
]
foreach recipe : mapgen_recipes
  mapgen_recipe = executable('mapgen_' + recipe + '_recipe',
    'map_recipes/' + recipe + '.cpp',
    dependencies: mapgen_dep,
    install: false,
  )
  test('mapgen-recipe-' + recipe.replace('_', '-'), mapgen_recipe,
    args: [meson.current_build_dir() / 'recipe-test' / recipe.replace('_', '-')],
    timeout: 300,
    suite: 'mapgen-research',
  )
endforeach

mapgen_recipe_environment_test = executable('mapgen_recipe_environment_test',
  '../tests/test_recipe_environment.cpp',
  dependencies: mapgen_dep,
  install: false,
)
test('mapgen-recipe-environment', mapgen_recipe_environment_test, timeout: 300, suite: 'mapgen-research')

# The multiplayer portfolio exporter and candidate tracers (mp_portfolio/README.md). No tests of their
# own; the portfolio comparisons drive them by hand.
executable('mapgen_mp_portfolio_export', 'mp_portfolio/export.cpp', dependencies: mapgen_dep, install: false)
executable('mapgen_mp_candidate_trace', 'mp_portfolio/trace_candidates.cpp', dependencies: mapgen_dep, install: false)
executable('mapgen_span_trace', 'mp_portfolio/span_trace.cpp', dependencies: mapgen_dep, install: false)

# The Python research tests, run from src/mapgen as Quake-MapGen ran them from its root. The groups
# that need Quake-MapGen's untracked out/ scripts, game trees, pinned compilers or Node are not
# registered; VIBESTUDIO.md lists them with what they need. The native groups run their contract
# cases here and skip their integration cases unless the private native build and retained out/
# inputs are present. Quake-MapGen asks for Python 3.10 or newer.
mapgen_research_python = find_program('python', 'python3', required: false, version: '>=3.10')
if mapgen_research_python.found()
  mapgen_unittest_groups = {
    'arrival_audit': 120,
    'arrival_bindings': 120,
    'atrium_routes': 180,
    'braid_compare': 120,
    'fold_audit': 120,
    'gate_audit': 120,
    'hinge_compare': 120,
    'lockyard_audit': 120,
    'mp_portfolio': 120,
    'mp_ranking': 120,
    'mp_topology': 120,
    'native_ascent': 120,
    'native_motif_circuits': 120,
    'native_movement': 120,
    'native_ravel': 120,
    'native_recovery': 120,
    'native_teleport': 120,
    'native_upper_link': 120,
    'packing_audit': 120,
    'ravel_audit': 120,
    'recipe_native_display': 120,
    'span_compare': 180,
    'spawn_separation_study': 120,
    'teleporter_arrivals': 120,
    'upper_link_controls': 120,
  }
  # The Quake Live atlas projects surfaces with the optional Shapely package.
  if run_command(mapgen_research_python, '-B', '-c', 'import shapely', check: false).returncode() == 0
    mapgen_unittest_groups += {'ql_atlas': 120}
  endif
  foreach name, timeout : mapgen_unittest_groups
    test('mapgen-py-' + name.replace('_', '-'), mapgen_research_python,
      args: ['-B', '-m', 'unittest', 'discover', '-s', 'tests', '-p', 'test_' + name + '.py'],
      workdir: mapgen_root,
      timeout: timeout,
      suite: 'mapgen-research',
    )
  endforeach

  # Test files Quake-MapGen runs as scripts.
  foreach name : ['map_study', 'map_study_q1q3', 'recipe_geometry', 'drop_audit']
    test('mapgen-py-' + name.replace('_', '-'), mapgen_research_python,
      args: ['-B', mapgen_root / 'tests' / ('test_' + name + '.py')],
      workdir: mapgen_root,
      timeout: 120,
      suite: 'mapgen-research',
    )
  endforeach

  # Game profiles: schema, citation coverage, the facts fixture, generator cross-checks, the freshness
  # of the header this build generated, and the mutation controls.
  test('mapgen-py-profile-schema', mapgen_research_python,
    args: ['-B', mapgen_root / 'tools/profiles/check_profiles.py', '--repo', mapgen_root,
      '--header', mapgen_profile_tables.full_path(), '--self-test'],
    depends: mapgen_profile_tables,
    workdir: mapgen_root,
    timeout: 120,
    suite: 'mapgen-research',
  )
  # data/ holds no image, WAL, BSP, WAD or archive bytes.
  test('mapgen-py-data-no-asset-bytes', mapgen_research_python,
    args: ['-B', mapgen_root / 'tools/asset_registry/scan_data_bytes.py', '--root', mapgen_root / 'data', '--self-test'],
    workdir: mapgen_root,
    timeout: 300,
    suite: 'mapgen-research',
  )

  # Benchmark v1 checks that need no compilers or assets (cmake/QmgBenchmarkTests.cmake). The pin is
  # the SHA-256 of benchmark/pr-subset-v1.json, as Quake-MapGen records it.
  mapgen_benchmark_checks = {
    'benchmark-compare-selftest': ['compare.py', ['--self-test']],
    'rc-ledger': ['ledger_check.py', []],
    'rc-ledger-parity': ['parity.py', []],
    'docs-claims': ['docs_claims.py', []],
    'rc-benchmark-selftest': ['rc_benchmark.py', ['--self-test',
      '--subset', mapgen_root / 'tools/benchmark/pr-subset-v1.json',
      '--subset-sha256', '0556b76dad32cab6a99e65b25a56221676a282e55952945cea22b29db2f24cac']],
  }
  foreach name, command : mapgen_benchmark_checks
    test('mapgen-py-' + name, mapgen_research_python,
      args: ['-B', mapgen_root / 'tools/benchmark' / command[0]] + command[1],
      workdir: mapgen_root,
      timeout: 120,
      suite: 'mapgen-research',
    )
  endforeach
endif
