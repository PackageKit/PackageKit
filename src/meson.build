packagekit_src_include = include_directories('.')

pk_resources = gnome.compile_resources(
    'pk-resources', 'packagekit.gresource.xml',
    source_dir: join_paths(source_root, 'data'),
    c_name: 'pk'
)

pk_daemon_shared_src = files(
    'pk-dbus.c',
    'pk-dbus.h',
    'pk-transaction.c',
    'pk-transaction.h',
    'pk-transaction-private.h',
    'pk-backend.c',
    'pk-backend.h',
    'pk-backend-job.c',
    'pk-backend-job.h',
    'pk-backend-spawn.c',
    'pk-backend-spawn.h',
    'pk-backend-protocol.c',
    'pk-backend-protocol.h',
    'pk-shared.c',
    'pk-shared.h',
    'pk-engine.h',
    'pk-engine.c',
    'pk-scheduler.c',
    'pk-scheduler.h',
    'pk-transaction-db.c',
    'pk-transaction-db.h',
    'pk-proxy-store.c',
    'pk-proxy-store.h',
)

packagekit_direct_exe = executable(
    'packagekit-direct',
    'pk-backend.c',
    'pk-backend.h',
    'pk-backend-job.c',
    'pk-backend-job.h',
    'pk-backend-spawn.c',
    'pk-backend-spawn.h',
    'pk-backend-protocol.c',
    'pk-backend-protocol.h',
    'pk-direct.c',
    'pk-shared.c',
    'pk-shared.h',
    dependencies: [
        libpackagekit_dep,
        jansson_dep,
        libsystemd,
        elogind,
        gmodule_dep,
    ],
    c_args: [
        '-DG_LOG_DOMAIN="PackageKit"',
        '-DVERSION="@0@"'.format(meson.project_version()),
        '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
        '-DPACKAGE_LOCALE_DIR="@0@"'.format(package_locale_dir),
    ],
    install: true,
    install_dir: get_option('libexecdir'),
)

packagekitd_exe = executable(
    'packagekitd',
    'pk-main.c',
    pk_daemon_shared_src,
    pk_resources,
    dependencies: [
        libpackagekit_dep,
        jansson_dep,
        libsystemd,
        elogind,
        polkit_dep,
        gmodule_dep,
        sqlite3_dep,
    ],
    c_args: [
        '-DPK_BUILD_DAEMON=1',
        '-DG_LOG_DOMAIN="PackageKit"',
        '-DLIBEXECDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libexecdir'))),
        '-DVERSION="@0@"'.format(meson.project_version()),
        '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
        '-DPACKAGE_LOCALE_DIR="@0@"'.format(package_locale_dir),
        '-DPK_DB_DIR="@0@"'.format(pk_db_dir),
    ],
    export_dynamic: true,
    install: true,
    install_dir: get_option('libexecdir'),
)

if get_option('python-backend')
    subdir('python/packagekit_backend')
endif

install_data(
    'org.freedesktop.packagekit1.xml',
    'org.freedesktop.packagekit1.Transaction.xml',
    install_dir: join_paths(get_option('datadir'), 'dbus-1', 'interfaces')
)
