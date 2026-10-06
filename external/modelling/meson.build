xatlas_python = find_program('python', 'python3')
xatlas_source = custom_target('xatlas_indexed',
  input: ['xatlas/xatlas.cpp', 'xatlas/xatlas.h', 'xatlas/UPSTREAM.json'], output: ['xatlas_indexed.cpp', 'xatlas.h'],
  command: [xatlas_python, files('prepare_xatlas.py'), '@INPUT0@', '@INPUT1@', '@INPUT2@', '@OUTPUT0@', '@OUTPUT1@'],
)
xatlas_includes = include_directories('.', is_system: true)
# All work runs on VibeStudio's existing document worker. This keeps allocation
# regions and cancellation on the caller's thread and avoids nested thread pools.
xatlas_library = static_library('vibestudio_xatlas', xatlas_source,
  include_directories: xatlas_includes,
  cpp_args: ['-DXA_MULTITHREADED=0', '-DXA_DEBUG=0', '-DNDEBUG', '-D_CRT_SECURE_NO_WARNINGS'],
  override_options: ['werror=false', 'warning_level=0', 'optimization=2'], install: false,
)
xatlas_dep = declare_dependency(link_with: xatlas_library, include_directories: xatlas_includes, sources: xatlas_source[1])
install_data('xatlas/LICENSE', 'xatlas/LICENSE-THIRD-PARTY.txt', 'xatlas/UPSTREAM.json', 'xatlas/VIBESTUDIO.md',
  install_dir: get_option('datadir') / 'vibestudio/licenses/xatlas')
