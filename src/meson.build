conf = configuration_data()
conf.set('VIBESTUDIO_VERSION', meson.project_version())
conf.set('VIBESTUDIO_GITHUB_REPO', get_option('github_repo'))
conf.set('VIBESTUDIO_UPDATE_CHANNEL', get_option('update_channel'))

configure_file(
  input: 'vibestudio_config.h.in',
  output: 'vibestudio_config.h',
  configuration: conf,
)

vibestudio_includes = include_directories('.')

core_sources = files(
  'core/operation_state.cpp',
  'core/studio_manifest.cpp',
  'core/studio_settings.cpp',
)

vibestudio_core = static_library(
  'vibestudio_core',
  core_sources,
  include_directories: vibestudio_includes,
  dependencies: [qt6_core_dep],
  install: true,
)

vibestudio_core_dep = declare_dependency(
  include_directories: vibestudio_includes,
  dependencies: [qt6_core_dep],
  link_with: [vibestudio_core],
)

install_headers(files('core/operation_state.h', 'core/studio_manifest.h', 'core/studio_settings.h'), subdir: 'vibestudio/core')

app_sources = files(
  'main.cpp',
  'app/application_shell.cpp',
  'app/ui_primitives.cpp',
  'cli/cli.cpp',
)

vibestudio = executable(
  'vibestudio',
  app_sources,
  include_directories: vibestudio_includes,
  dependencies: [qt6_dep, vibestudio_core_dep],
  install: true,
)

core_smoke_test = executable(
  'core_smoke_test',
  ['tests/core_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('core-smoke', core_smoke_test)

settings_smoke_test = executable(
  'settings_smoke_test',
  ['tests/settings_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('settings-smoke', settings_smoke_test)

operation_state_smoke_test = executable(
  'operation_state_smoke_test',
  ['tests/operation_state_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('operation-state-smoke', operation_state_smoke_test)

ui_primitives_smoke_test = executable(
  'ui_primitives_smoke_test',
  ['tests/ui_primitives_smoke_test.cpp', 'app/ui_primitives.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [qt6_dep, vibestudio_core_dep],
  install: false,
)

ui_primitives_test_env = []
if host_machine.system() != 'windows'
  ui_primitives_test_env += 'QT_QPA_PLATFORM=offscreen'
endif

test(
  'ui-primitives-smoke',
  ui_primitives_smoke_test,
  env: ui_primitives_test_env,
)
