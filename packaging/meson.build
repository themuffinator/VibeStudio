# Desktop integration for Linux and other freedesktop systems: the launcher
# entry, AppStream metadata, and the brand icon at the icon theme's sizes. The
# AppImage is built from this installed layout (scripts/package_appimage.py).
app_id = 'io.github.themuffinator.VibeStudio'
if host_machine.system() not in ['windows', 'darwin']
  install_data('linux' / app_id + '.desktop', install_dir: get_option('datadir') / 'applications')
  install_data('linux' / app_id + '.metainfo.xml', install_dir: get_option('datadir') / 'metainfo')
  foreach size : [16, 22, 24, 32, 48, 64, 128, 256, 512]
    install_data('../assets/branding/icons/png/vibestudio-@0@.png'.format(size),
      install_dir: get_option('datadir') / 'icons' / 'hicolor' / '@0@x@0@'.format(size) / 'apps',
      rename: app_id + '.png',
    )
  endforeach
  install_data('../assets/branding/icons/vibestudio.svg',
    install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
    rename: app_id + '.svg',
  )
endif
