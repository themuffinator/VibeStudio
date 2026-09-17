# Compiled translation catalogs.
#
# lrelease turns the checked-in .ts catalogs into the .qm files QTranslator
# loads at run time. It is optional: without it the application falls back to
# the source language rather than failing to build.
#
# Outputs land in <builddir>/i18n, which a development binary at
# <builddir>/src/vibestudio finds through its "<app dir>/../i18n" search path,
# and install into <datadir>/vibestudio/i18n for packaged builds.

lrelease = find_program('lrelease-qt6', 'lrelease6', 'lrelease', required: false)

translation_locales = [
  'ar',
  'bn',
  'de',
  'en',
  'es',
  'fr',
  'hi',
  'id',
  'ja',
  'ko',
  'mr',
  'pcm',
  'pseudo',
  'pt_BR',
  'ru',
  'ta',
  'te',
  'tr',
  'ur',
  'vi',
  'zh_Hans',
]

compiled_translations = []
if lrelease.found()
  foreach locale : translation_locales
    compiled_translations += custom_target(
      'translation-@0@'.format(locale),
      input: 'vibestudio_@0@.ts'.format(locale),
      output: 'vibestudio_@0@.qm'.format(locale),
      command: [lrelease, '-silent', '@INPUT@', '-qm', '@OUTPUT@'],
      build_by_default: true,
      install: true,
      install_dir: get_option('datadir') / 'vibestudio' / 'i18n',
    )
  endforeach
else
  message('lrelease was not found; translation catalogs will not be compiled and the application will use the source language.')
endif
