packagekit_src_include = include_directories('.')

pk_resources = gnome.compile_resources(
  'pk-resources', 'packagekit.gresource.xml',
  source_dir: join_paths(meson.source_root(), 'data'),
  c_name: 'pk'
)

shared_sources = files(
  'pk-dbus.c',
  'pk-dbus.h',
  'pk-transaction.c',
  'pk-transaction.h',
  'pk-transaction-private.h',
  'pk-backend.c',
  'pk-backend.h',
  'pk-backend-job.c',
  'pk-backend-job.h',
  'pk-shared.c',
  'pk-shared.h',
  'pk-spawn.c',
  'pk-spawn.h',
  'pk-engine.h',
  'pk-engine.c',
  'pk-backend-spawn.h',
  'pk-backend-spawn.c',
  'pk-scheduler.c',
  'pk-scheduler.h',
  'pk-transaction-db.c',
  'pk-transaction-db.h',
)

packagekit_direct_exec = executable(
  'packagekit-direct',
  'pk-backend.c',
  'pk-backend.h',
  'pk-backend-job.c',
  'pk-backend-job.h',
  'pk-direct.c',
  'pk-shared.c',
  'pk-shared.h',
  'pk-spawn.c',
  'pk-spawn.h',
  'pk-backend-spawn.h',
  'pk-backend-spawn.c',
  dependencies: [
    packagekit_glib2_dep,
    libsystemd,
    elogind,
    gmodule_dep,
  ],
  install: true,
  install_dir: get_option('libexecdir'),
  c_args: [
    '-DG_LOG_DOMAIN="PackageKit"',
    '-DLIBDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libdir'))),
    '-DSYSCONFDIR="@0@"'.format(get_option('sysconfdir')),
    '-DVERSION="@0@"'.format(meson.project_version()),
    '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
    '-DPACKAGE_LOCALE_DIR="@0@"'.format(package_locale_dir),
  ]
)

packagekitd_exec = executable(
  'packagekitd',
  'pk-main.c',
  shared_sources,
  pk_resources,
  dependencies: [
    packagekit_glib2_dep,
    libsystemd,
    elogind,
    polkit_dep,
    gmodule_dep,
    sqlite3_dep,
  ],
  install: true,
  install_dir: get_option('libexecdir'),
  c_args: [
    '-DPK_BUILD_DAEMON=1',
    '-DG_LOG_DOMAIN="PackageKit"',
    '-DLIBDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libdir'))),
    '-DDATADIR="@0@"'.format(join_paths(get_option('prefix'), get_option('datadir'))),
    '-DLIBEXECDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libexecdir'))),
    '-DVERSION="@0@"'.format(meson.project_version()),
    '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
    '-DPACKAGE_LOCALE_DIR="@0@"'.format(package_locale_dir),
    '-DLOCALSTATEDIR="@0@"'.format(local_state_dir),
    '-DPK_DB_DIR="@0@"'.format(pk_db_dir),
  ]
)

install_data(
  'org.freedesktop.PackageKit.xml',
  'org.freedesktop.PackageKit.Transaction.xml',
  install_dir: join_paths(get_option('datadir'), 'dbus-1', 'interfaces')
)

pk_self_test_exec = executable(
  'pk-self-test',
  'pk-self-test.c',
  shared_sources,
  pk_resources,
  dependencies: [
    packagekit_glib2_dep,
    libsystemd,
    elogind,
    polkit_dep,
    gmodule_dep,
    sqlite3_dep,
  ],
  c_args: [
    '-DPK_BUILD_DAEMON=1',
    '-DPK_DB_DIR="."',
    '-DLIBDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libdir'))),
    '-DDATADIR="@0@"'.format(join_paths(get_option('prefix'), get_option('datadir'))),
    '-DLIBEXECDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libexecdir'))),
    '-DTESTDATADIR="@0@"'.format(test_data_dir),
    '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
    '-DLOCALSTATEDIR="@0@"'.format(local_state_dir),
    '-DSOURCEROOTDIR="@0@"'.format(meson.source_root()),
  ]
)

test(
  'pk-self-test',
  pk_self_test_exec,
  depends: packagekit_test_py,
  env: [
  'PYTHONPATH=@0@'.format(join_paths(meson.build_root(), 'lib', 'python')),
  ],
  timeout: 360,
)
