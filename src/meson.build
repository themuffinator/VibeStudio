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
  'core/advanced_studio.cpp',
  'core/ai_connectors.cpp',
  'core/ai_workflows.cpp',
  'core/asset_tools.cpp',
  'core/bsp_inspect.cpp',
  'core/build_pipeline.cpp',
  'core/compiler_artifact_validation.cpp',
  'core/compiler_known_issues.cpp',
  'core/compiler_profiles.cpp',
  'core/compiler_registry.cpp',
  'core/compiler_runner.cpp',
  'core/deflate.cpp',
  'core/document_watch.cpp',
  'core/editor_profiles.cpp',
  'core/entity_definitions.cpp',
  'core/ericw_map_preflight.cpp',
  'core/game_installation.cpp',
  'core/idtech_image.cpp',
  'core/level_map.cpp',
  'core/localization.cpp',
  'core/map_assets.cpp',
  'core/map_geometry.cpp',
  'core/map_render.cpp',
  'core/model_mesh.cpp',
  'core/operation_state.cpp',
  'core/package_archive.cpp',
  'core/package_compare.cpp',
  'core/package_preview.cpp',
  'core/package_staging.cpp',
  'core/parser_fuzz.cpp',
  'core/project_manifest.cpp',
  'core/studio_semantics.cpp',
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

install_headers(files('core/advanced_studio.h', 'core/ai_connectors.h', 'core/ai_workflows.h', 'core/asset_tools.h', 'core/bsp_inspect.h', 'core/build_pipeline.h', 'core/compiler_artifact_validation.h', 'core/compiler_known_issues.h', 'core/compiler_profiles.h', 'core/compiler_registry.h', 'core/compiler_runner.h', 'core/deflate.h', 'core/document_watch.h', 'core/ericw_map_preflight.h', 'core/editor_profiles.h', 'core/entity_definitions.h', 'core/game_installation.h', 'core/idtech_image.h', 'core/level_map.h', 'core/localization.h', 'core/map_assets.h', 'core/map_geometry.h', 'core/map_render.h', 'core/model_mesh.h', 'core/operation_state.h', 'core/package_archive.h', 'core/package_compare.h', 'core/package_preview.h', 'core/package_staging.h', 'core/parser_fuzz.h', 'core/project_manifest.h', 'core/studio_semantics.h', 'core/studio_manifest.h', 'core/studio_settings.h'), subdir: 'vibestudio/core')

app_sources = files(
  'main.cpp',
  'app/application_shell.cpp',
  'app/asset_views.cpp',
  'app/code_editor.cpp',
  'app/map_viewport.cpp',
  'app/model_viewport.cpp',
  'app/studio_actions.cpp',
  'app/studio_charts.cpp',
  'app/studio_icons.cpp',
  'app/studio_layout.cpp',
  'app/studio_runtime.cpp',
  'app/studio_theme.cpp',
  'app/syntax_highlight.cpp',
  'app/ui_primitives.cpp',
  'cli/cli.cpp',
)

app_moc_headers = files(
  'app/application_shell.h',
  'app/asset_views.h',
  'app/map_viewport.h',
  'app/model_viewport.h',
  'app/studio_actions.h',
  'app/studio_charts.h',
  'app/studio_layout.h',
  'app/syntax_highlight.h',
)

app_moc_sources = qt6.preprocess(
  moc_headers: app_moc_headers,
  include_directories: vibestudio_includes,
  dependencies: qt6_dep,
)

vibestudio = executable(
  'vibestudio',
  app_sources,
  app_moc_sources,
  include_directories: vibestudio_includes,
  dependencies: [qt6_dep, vibestudio_core_dep],
  install: true,
)

deflate_smoke_test = executable(
  'deflate_smoke_test',
  ['tests/deflate_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('deflate-smoke', deflate_smoke_test)

idtech_image_smoke_test = executable(
  'idtech_image_smoke_test',
  ['tests/idtech_image_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('idtech-image-smoke', idtech_image_smoke_test)

map_assets_smoke_test = executable(
  'map_assets_smoke_test',
  ['tests/map_assets_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('map-assets-smoke', map_assets_smoke_test)

map_geometry_smoke_test = executable(
  'map_geometry_smoke_test',
  ['tests/map_geometry_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('map-geometry-smoke', map_geometry_smoke_test)

map_render_smoke_test = executable(
  'map_render_smoke_test',
  ['tests/map_render_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('map-render-smoke', map_render_smoke_test)

bsp_inspect_smoke_test = executable(
  'bsp_inspect_smoke_test',
  ['tests/bsp_inspect_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('bsp-inspect-smoke', bsp_inspect_smoke_test)

build_pipeline_smoke_test = executable(
  'build_pipeline_smoke_test',
  ['tests/build_pipeline_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('build-pipeline-smoke', build_pipeline_smoke_test)

core_smoke_test = executable(
  'core_smoke_test',
  ['tests/core_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('core-smoke', core_smoke_test)

ai_connectors_smoke_test = executable(
  'ai_connectors_smoke_test',
  ['tests/ai_connectors_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('ai-connectors-smoke', ai_connectors_smoke_test)

ai_workflows_smoke_test = executable(
  'ai_workflows_smoke_test',
  ['tests/ai_workflows_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('ai-workflows-smoke', ai_workflows_smoke_test)

advanced_studio_smoke_test = executable(
  'advanced_studio_smoke_test',
  ['tests/advanced_studio_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('advanced-studio-smoke', advanced_studio_smoke_test)

compiler_registry_smoke_test = executable(
  'compiler_registry_smoke_test',
  ['tests/compiler_registry_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('compiler-registry-smoke', compiler_registry_smoke_test)

compiler_known_issues_smoke_test = executable(
  'compiler_known_issues_smoke_test',
  ['tests/compiler_known_issues_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('compiler-known-issues-smoke', compiler_known_issues_smoke_test)

compiler_profiles_smoke_test = executable(
  'compiler_profiles_smoke_test',
  ['tests/compiler_profiles_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('compiler-profiles-smoke', compiler_profiles_smoke_test)

compiler_artifact_validation_smoke_test = executable(
  'compiler_artifact_validation_smoke_test',
  ['tests/compiler_artifact_validation_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('compiler-artifact-validation-smoke', compiler_artifact_validation_smoke_test)

ericw_map_preflight_smoke_test = executable(
  'ericw_map_preflight_smoke_test',
  ['tests/ericw_map_preflight_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('ericw-map-preflight-smoke', ericw_map_preflight_smoke_test)

level_map_smoke_test = executable(
  'level_map_smoke_test',
  ['tests/level_map_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('level-map-smoke', level_map_smoke_test)

localization_smoke_test = executable(
  'localization_smoke_test',
  ['tests/localization_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('localization-smoke', localization_smoke_test)

studio_semantics_smoke_test = executable(
  'studio_semantics_smoke_test',
  ['tests/studio_semantics_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('studio-semantics-smoke', studio_semantics_smoke_test)

editor_profiles_smoke_test = executable(
  'editor_profiles_smoke_test',
  ['tests/editor_profiles_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('editor-profiles-smoke', editor_profiles_smoke_test)

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

game_installation_smoke_test = executable(
  'game_installation_smoke_test',
  ['tests/game_installation_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('game-installation-smoke', game_installation_smoke_test)

package_archive_smoke_test = executable(
  'package_archive_smoke_test',
  ['tests/package_archive_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('package-archive-smoke', package_archive_smoke_test)

package_preview_smoke_test = executable(
  'package_preview_smoke_test',
  ['tests/package_preview_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('package-preview-smoke', package_preview_smoke_test)

asset_tools_smoke_test = executable(
  'asset_tools_smoke_test',
  ['tests/asset_tools_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('asset-tools-smoke', asset_tools_smoke_test)

package_staging_smoke_test = executable(
  'package_staging_smoke_test',
  ['tests/package_staging_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('package-staging-smoke', package_staging_smoke_test)

project_manifest_smoke_test = executable(
  'project_manifest_smoke_test',
  ['tests/project_manifest_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('project-manifest-smoke', project_manifest_smoke_test)

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

document_watch_smoke_test = executable(
  'document_watch_smoke_test',
  ['tests/document_watch_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('document-watch-smoke', document_watch_smoke_test)

entity_definitions_smoke_test = executable(
  'entity_definitions_smoke_test',
  ['tests/entity_definitions_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('entity-definitions-smoke', entity_definitions_smoke_test)

model_mesh_smoke_test = executable(
  'model_mesh_smoke_test',
  ['tests/model_mesh_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('model-mesh-smoke', model_mesh_smoke_test)

package_compare_smoke_test = executable(
  'package_compare_smoke_test',
  ['tests/package_compare_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('package-compare-smoke', package_compare_smoke_test)

parser_fuzz_smoke_test = executable(
  'parser_fuzz_smoke_test',
  ['tests/parser_fuzz_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('parser-fuzz-smoke', parser_fuzz_smoke_test)

corrupt_fixture_smoke_test = executable(
  'corrupt_fixture_smoke_test',
  ['tests/corrupt_fixture_smoke_test.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [vibestudio_core_dep],
  install: false,
)

test('corrupt-fixture-smoke', corrupt_fixture_smoke_test)

studio_theme_smoke_test = executable(
  'studio_theme_smoke_test',
  ['tests/studio_theme_smoke_test.cpp', 'app/studio_theme.cpp', 'app/studio_icons.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [qt6_dep, vibestudio_core_dep],
  install: false,
)

test(
  'studio-theme-smoke',
  studio_theme_smoke_test,
  env: ui_primitives_test_env,
)

studio_runtime_smoke_test = executable(
  'studio_runtime_smoke_test',
  ['tests/studio_runtime_smoke_test.cpp', 'app/studio_runtime.cpp'],
  include_directories: vibestudio_includes,
  dependencies: [qt6_dep, vibestudio_core_dep],
  install: false,
)

test(
  'studio-runtime-smoke',
  studio_runtime_smoke_test,
  env: ui_primitives_test_env,
)
