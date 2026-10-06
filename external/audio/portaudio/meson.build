# Private, pinned PortAudio hosts; never run upstream's SDK discovery/downloads.
pa_option = get_option('audio_duplex')
pa_enabled = not pa_option.disabled()
pa_platform_deps = [dependency('threads')]
pa_platform_sources = []
pa_host_args = []
pa_os_includes = []
pa_host_includes = []
pa_python = find_program('python', 'python3')
pa_patch = files('patches/apply.py')

if host_machine.system() == 'windows'
  foreach lib : ['winmm', 'ole32', 'uuid']
    pa_platform_deps += xiph_c.find_library(lib)
  endforeach
  pa_os_includes = include_directories('src/os/win', is_system: true)
  pa_platform_sources = files('src/os/win/pa_win_coinitialize.c', 'src/os/win/pa_win_hostapis.c',
    'src/os/win/pa_win_util.c', 'src/os/win/pa_win_version.c', 'src/os/win/pa_win_waveformat.c')
  if xiph_c.get_argument_syntax() == 'msvc'
    pa_platform_sources += files('src/os/win/pa_x86_plain_converters.c')
  endif
  if pa_enabled
    pa_platform_sources += custom_target('portaudio-wasapi-patched',
      input: 'src/hostapi/wasapi/pa_win_wasapi.c', output: 'pa_win_wasapi.c',
      command: [pa_python, pa_patch, '@INPUT@', '@OUTPUT@'],
      depend_files: [files('UPSTREAM.json'), pa_patch])
  endif
  pa_host_args = ['-DPA_USE_WASAPI=1', '-D_CRT_SECURE_NO_WARNINGS=1']
elif host_machine.system() == 'darwin'
  pa_platform_deps += dependency('appleframeworks', modules: ['CoreAudio', 'AudioToolbox',
    'AudioUnit', 'CoreFoundation', 'CoreServices'])
  # Generate all CoreAudio sources/headers together, so quoted includes resolve
  # to the reviewed atomic-xrun header rather than the untouched upstream copy.
  if pa_enabled
    foreach name : ['pa_mac_core.c', 'pa_mac_core_blocking.c', 'pa_mac_core_utilities.c',
                    'pa_mac_core_internal.h', 'pa_mac_core_blocking.h', 'pa_mac_core_utilities.h']
      pa_platform_sources += custom_target('portaudio-' + name,
        input: 'src/hostapi/coreaudio' / name, output: name,
        command: [pa_python, pa_patch, '@INPUT@', '@OUTPUT@'],
        depend_files: [files('UPSTREAM.json'), pa_patch])
    endforeach
  endif
  pa_host_args = ['-DPA_USE_COREAUDIO=1']
elif host_machine.system() == 'linux'
  pa_alsa = dependency('alsa', required: pa_option)
  pa_enabled = pa_enabled and pa_alsa.found()
  pa_platform_deps += pa_alsa
  pa_platform_sources = files('src/hostapi/alsa/pa_linux_alsa.c')
  pa_host_args = ['-DPA_USE_ALSA=1']
else
  if pa_option.enabled()
    error('audio_duplex currently provides WASAPI, CoreAudio and ALSA hosts.')
  endif
  pa_enabled = false
endif
if host_machine.system() != 'windows'
  pa_platform_sources += files('src/os/unix/pa_unix_hostapis.c',
    'src/os/unix/pa_unix_util.c', 'src/os/unix/pa_pthread_util.c')
  pa_os_includes = include_directories('src/os/unix', is_system: true)
  pa_platform_deps += xiph_math
endif
pa_headers = include_directories('include', 'patches', is_system: true)
pa_common_headers = include_directories('src/common', is_system: true)
audio_duplex_dep = declare_dependency(include_directories: pa_headers)
if pa_enabled
  pa_sources = files('src/common/pa_allocation.c', 'src/common/pa_converters.c',
    'src/common/pa_cpuload.c', 'src/common/pa_debugprint.c', 'src/common/pa_dither.c',
    'src/common/pa_front.c', 'src/common/pa_process.c', 'src/common/pa_ringbuffer.c',
    'src/common/pa_stream.c', 'src/common/pa_trace.c')
  pa_endian = host_machine.endian() == 'little' ? '-DPA_LITTLE_ENDIAN=1' : '-DPA_BIG_ENDIAN=1'
  # ALSA uses GNU typeof and Unix pthread feature declarations, as in upstream.
  pa_c_standard = host_machine.system() == 'windows' ? 'c11' : 'gnu11'
  pa_library = static_library('vibestudio_portaudio', pa_sources, pa_platform_sources,
    include_directories: [pa_headers, pa_common_headers, pa_os_includes, pa_host_includes],
    c_args: pa_host_args + [pa_endian], dependencies: pa_platform_deps,
    override_options: ['c_std=' + pa_c_standard, 'werror=false', 'warning_level=0'], install: false)
  audio_duplex_dep = declare_dependency(link_with: pa_library, include_directories: pa_headers,
    dependencies: pa_platform_deps)
endif
install_data('LICENSE.txt', 'UPSTREAM.json', 'VIBESTUDIO.md',
  install_dir: get_option('datadir') / 'vibestudio/licenses/portaudio')
