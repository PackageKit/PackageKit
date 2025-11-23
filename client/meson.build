
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

if get_option('pkgctl')

executable(
  'pkgctl',
  [
    'pkgctl.c',
    'pkgc-context.c',
    'pkgc-manage.c',
    'pkgc-monitor.c',
    'pkgc-query.c',
    'pkgc-repo.c',
    'pkgc-util.c',
  ],
  dependencies: [
    packagekit_glib2_dep,
    jansson_dep],
  install: true,
  c_args: [
    '-DPK_COMPILATION=1',
    '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
    '-DPACKAGE_LOCALE_DIR="@0@"'.format(package_locale_dir),
    '-DVERSION="@0@"'.format(meson.project_version()),
  ]
)

endif
