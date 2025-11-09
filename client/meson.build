
if get_option('offline_update')
  executable(
    'pk-offline-update',
    'pk-offline-update.c',
    dependencies: [
      packagekit_glib2_dep,
      libsystemd,
    ],
    install: true,
    install_dir: get_option('libexecdir'),
    c_args: [
      '-DPK_COMPILATION=1',
        '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
      '-DPACKAGE_LOCALE_DIR="@0@"'.format(package_locale_dir),
    ]
  )
endif

subdir('pkcon')
