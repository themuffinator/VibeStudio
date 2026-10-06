# Private Xiph C libraries; VibeStudio's decoder/service code remains C++20.
add_languages('c', native: false)
xiph_c = meson.get_compiler('c')
xiph_includes = include_directories('xiph', 'libogg/include', 'libvorbis/include', is_system: true)
xiph_adapter = meson.current_source_dir() / 'xiph/vibestudio_xiph_memory.h'
xiph_args = xiph_c.get_argument_syntax() == 'msvc' ? ['/FI' + xiph_adapter] : ['-include', xiph_adapter]
xiph_args += ['-D_CRT_SECURE_NO_WARNINGS=1']
if host_machine.system() != 'windows'
  xiph_args += ['-DHAVE_ALLOCA_H=1']
endif
xiph_math = xiph_c.find_library('m', required: false)
xiph_sources = files(
  'libogg/src/bitwise.c', 'libogg/src/framing.c',
  'libvorbis/lib/analysis.c', 'libvorbis/lib/bitrate.c', 'libvorbis/lib/block.c',
  'libvorbis/lib/codebook.c', 'libvorbis/lib/envelope.c', 'libvorbis/lib/floor0.c',
  'libvorbis/lib/floor1.c', 'libvorbis/lib/info.c', 'libvorbis/lib/lookup.c',
  'libvorbis/lib/lpc.c', 'libvorbis/lib/lsp.c', 'libvorbis/lib/mapping0.c',
  'libvorbis/lib/mdct.c', 'libvorbis/lib/psy.c',
  'libvorbis/lib/registry.c', 'libvorbis/lib/res0.c', 'libvorbis/lib/sharedbook.c',
  'libvorbis/lib/smallft.c', 'libvorbis/lib/synthesis.c', 'libvorbis/lib/vorbisfile.c',
  'libvorbis/lib/window.c',
)
xiph_library = static_library('vibestudio_xiph', xiph_sources,
  include_directories: xiph_includes, c_args: xiph_args,
  dependencies: xiph_math, override_options: ['werror=false', 'warning_level=0'],
  install: false,
)
xiph_decoder_dep = declare_dependency(link_with: xiph_library, include_directories: xiph_includes, dependencies: xiph_math)

# Unmodified libebur128 1.2.6, MIT with a BSD-3-Clause queue; see its notices.
meter_includes = include_directories('libebur128/ebur128', 'libebur128/ebur128/queue', is_system: true)
meter_library = static_library('vibestudio_ebur128', 'libebur128/ebur128/ebur128.c',
  include_directories: meter_includes, c_args: ['-D_USE_MATH_DEFINES=1', '-D_CRT_SECURE_NO_WARNINGS=1'],
  dependencies: xiph_math, override_options: ['werror=false', 'warning_level=0'], install: false,
)
audio_meter_dep = declare_dependency(link_with: meter_library, include_directories: meter_includes, dependencies: xiph_math)

subdir('portaudio')
