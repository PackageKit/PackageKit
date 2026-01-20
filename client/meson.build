
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

executable(
  'pkgcli',
  [
    'pkgcli.c',
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

if get_option('man_pages')
  xsltproc = find_program('xsltproc')
  custom_target(
    'pkgcli.1',
    input: 'pkgcli.xml',
    output: 'pkgcli.1',
    command: [
      xsltproc,
      '--nonet',
      '--output', '@OUTPUT@',
      'http://docbook.sourceforge.net/release/xsl-ns/current/manpages/docbook.xsl',
      '@INPUT@',
    ],
    install: true,
    install_dir: join_paths(get_option('datadir'), 'man', 'man1'),
  )
endif

if get_option('legacy_tools')
    subdir('pkcon')
endif
