project(
  'vibestudio',
  'cpp',
  # Read straight from the VERSION file: shelling out to `python` made configure
  # depend on a program named exactly "python", which does not exist on many
  # Linux and macOS installations that only provide `python3`.
  version: files('VERSION'),
  default_options: [
    'cpp_std=c++20',
    'warning_level=2',
    'werror=false',
  ],
)

qt6_modules = ['Core', 'Gui', 'Widgets', 'Network']
# Network is here for the opt-in AI transport (src/core/ai_transport.*), which
# the CLI and the shell share.
qt6_core_modules = ['Core', 'Gui', 'Network']
qt6 = import('qt6')

qt6_dep = dependency('qt6', modules: qt6_modules, method: 'config-tool', required: false)
if not qt6_dep.found()
  qt6_dep = dependency('qt6', modules: qt6_modules, method: 'pkg-config', required: false)
endif
if not qt6_dep.found()
  error('Qt 6 not found. Install Qt 6 and ensure qmake6 is on PATH or pkg-config is available.')
endif

qt6_core_dep = dependency('qt6', modules: qt6_core_modules, method: 'config-tool', required: false)
if not qt6_core_dep.found()
  qt6_core_dep = dependency('qt6', modules: qt6_core_modules, method: 'pkg-config', required: false)
endif
if not qt6_core_dep.found()
  error('Qt 6 Core/Gui/Network modules not found. Install Qt 6 and ensure qmake6 is on PATH or pkg-config is available.')
endif

# Qt Test drives the GUI interaction test. It ships with qtbase; a Qt install
# without it skips that one test instead of failing the build.
qt6_test_modules = ['Core', 'Gui', 'Widgets', 'Test']
qt6_test_dep = dependency('qt6', modules: qt6_test_modules, method: 'config-tool', required: false)
if not qt6_test_dep.found()
  qt6_test_dep = dependency('qt6', modules: qt6_test_modules, method: 'pkg-config', required: false)
endif

# Qt Multimedia plays sounds in the Audio browser. It is an add-on module, so
# a Qt install without it builds a studio that shows waveforms but cannot
# play them; -Daudio_playback=enabled makes its absence an error instead.
audio_playback_option = get_option('audio_playback')
qt6_multimedia_dep = dependency('', required: false)
if not audio_playback_option.disabled()
  qt6_multimedia_dep = dependency('qt6', modules: ['Core', 'Gui', 'Multimedia'], method: 'config-tool', required: false)
  if not qt6_multimedia_dep.found()
    qt6_multimedia_dep = dependency('qt6', modules: ['Core', 'Gui', 'Multimedia'], method: 'pkg-config', required: false)
  endif
  if audio_playback_option.enabled() and not qt6_multimedia_dep.found()
    error('audio_playback is enabled, but Qt 6 Multimedia was not found. Install the qtmultimedia module or pass -Daudio_playback=disabled.')
  endif
endif

install_data(
  'external/audio/r8brain-free-src/LICENSE',
  'external/audio/r8brain-free-src/LICENSE-OOURA.txt',
  'external/audio/r8brain-free-src/VIBESTUDIO.md',
  'external/audio/r8brain-free-src/UPSTREAM.json',
  install_dir: get_option('datadir') / 'vibestudio/licenses/r8brain-free-src',
)

install_data('docs/licenses/UNICODE-LICENSE.txt',
  install_dir: get_option('datadir') / 'vibestudio/licenses/unicode')

subdir('i18n')
foreach decoder : ['dr_libs', 'libogg', 'libvorbis']
  install_data(
    'external/audio' / decoder / (decoder == 'dr_libs' ? 'LICENSE' : 'COPYING'),
    'external/audio' / decoder / 'VIBESTUDIO.md',
    'external/audio' / decoder / 'UPSTREAM.json',
    install_dir: get_option('datadir') / 'vibestudio/licenses' / decoder,
  )
endforeach
subdir('external/audio')
install_data(
  'external/audio/libebur128/COPYING',
  'external/audio/libebur128/VIBESTUDIO.md',
  'external/audio/libebur128/UPSTREAM.json',
  'external/audio/libebur128/doc/license/queue.txt',
  'external/audio/libebur128/doc/license/R128Scan.txt',
  install_dir: get_option('datadir') / 'vibestudio/licenses/libebur128',
)
subdir('external/modelling')
install_data(
  'external/graphics/vulkan-headers/LICENSE.md',
  'external/graphics/vulkan-headers/LICENSES/Apache-2.0.txt',
  'external/graphics/vulkan-headers/LICENSES/MIT.txt',
  'external/graphics/vulkan-headers/VIBESTUDIO.md',
  'external/graphics/vulkan-headers/UPSTREAM.json',
  install_dir: get_option('datadir') / 'vibestudio/licenses/vulkan-headers',
)
subdir('src')
subdir('packaging')

python_for_tests = find_program('python', 'python3', required: false)
if python_for_tests.found()
  test('audio-portaudio-patch-smoke', python_for_tests,
    args: [files('src/tests/audio_portaudio_patch_test.py')])
  test(
    'release-pipeline-smoke',
    python_for_tests,
    args: [files('src/tests/release_pipeline_test.py')],
    # File/link provenance checks can be slow on Windows-mounted WSL trees.
    timeout: 900,
  )
  test(
    'source-companion-smoke',
    python_for_tests,
    args: [files('src/tests/source_companion_test.py')],
    timeout: 180,
  )
  test(
    'qt-runtime-smoke',
    python_for_tests,
    args: [files('src/tests/qt_runtime_test.py')],
    timeout: 60,
  )
  test(
    'package-translations-smoke',
    python_for_tests,
    args: [files('src/tests/package_translations_test.py')],
  )
  test(
    'documentation-version',
    python_for_tests,
    args: [files('scripts/sync_doc_versions.py'), '--check'],
  )
  test(
    'documentation-validation',
    python_for_tests,
    args: [files('scripts/validate_docs.py')],
  )
  test(
    'source-layout-validation',
    python_for_tests,
    args: [files('scripts/validate_source_layout.py')],
  )
  test(
    'credits-validation',
    python_for_tests,
    args: [files('scripts/validate_credits.py')],
  )
  # The committed SPIR-V and OpenGL shader text match src/core/shaders; with
  # glslangValidator on PATH every variant is compiled again and compared.
  test(
    'render-shaders-validation',
    python_for_tests,
    args: [files('scripts/build_render_shaders.py'), '--check'],
    timeout: 120,
  )
  test(
    'translation-extraction-validation',
    python_for_tests,
    args: [files('scripts/extract_translations.py'), '--check'],
  )
  test(
    'translation-extraction-smoke',
    python_for_tests,
    args: [files('src/tests/translation_extraction_test.py')],
  )
  test(
    'english-plurals-validation',
    python_for_tests,
    args: [files('scripts/english_plurals.py')],
  )
  # Releases, the changelog, brand artwork and the HTML documentation
  # (docs/RELEASING.md, docs/BRANDING.md). The branding and documentation
  # checks skip (exit 77) when fontTools or Markdown/Pygments are missing.
  test(
    'release-tooling-smoke',
    python_for_tests,
    args: [files('src/tests/release_tooling_test.py')],
    timeout: 120,
  )
  test(
    'changelog-validation',
    python_for_tests,
    args: [files('scripts/changelog.py'), 'check'],
  )
  test(
    'branding-validation',
    python_for_tests,
    args: [files('scripts/generate_branding.py'), '--check', '--skip-missing-tools'],
    timeout: 120,
  )
  test(
    'documentation-site',
    python_for_tests,
    args: [files('scripts/build_docs_site.py'), '--output', meson.current_build_dir() / 'docs-site-check', '--skip-missing-tools'],
    timeout: 120,
  )
endif
