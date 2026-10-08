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

# The 47 interface languages of core/localization.cpp plus pseudo-localization;
# scripts/validate_docs.py checks this list against the registry and i18n/*.ts.
translation_locales = [
  'am',
  'ar',
  'bg',
  'bn',
  'cs',
  'da',
  'de',
  'el',
  'en',
  'es',
  'es_419',
  'fa',
  'fi',
  'fil',
  'fr',
  'gu',
  'ha',
  'he',
  'hi',
  'hu',
  'id',
  'it',
  'ja',
  'kn',
  'ko',
  'mr',
  'ms',
  'nb',
  'nl',
  'pa',
  'pcm',
  'pl',
  'pseudo',
  'pt_BR',
  'pt_PT',
  'ro',
  'ru',
  'sv',
  'sw',
  'ta',
  'te',
  'th',
  'tr',
  'uk',
  'ur',
  'vi',
  'zh_Hans',
  'zh_Hant',
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
