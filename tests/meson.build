
pk_test_lib_exe = executable(
    'pk-test-library',
    'pk-test-library.c',
    pk_enum_type,
    packagekitprivate_sources,
    packagekit_glib2_sources,
    include_directories: packagekit_glib2_includes,
    dependencies: [
        glib_dep,
        gobject_dep,
        gio_dep,
        gio_unix_dep,
        config_dep,
    ],
    c_args: [
        '-DPK_COMPILATION=1',
        '-DG_LOG_DOMAIN="PackageKit"',
        '-DPK_OFFLINE_DESTDIR="/tmp/PackageKit-self-test"',
        '-DTESTDATADIR="@0@"'.format(test_data_dir),
    ],
    build_by_default: true,
    install: false,
)

test(
    'pk-test-library',
    pk_test_lib_exe,
    suite: 'main',
    timeout: 360,
)

pk_test_daemon_priv_exe = executable(
    'pk-test-daemon-internal',
    'pk-test-daemon-internal.c',
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
    include_directories: [packagekit_src_include],
    c_args: [
        '-DPK_BUILD_DAEMON=1',
        '-DPK_DB_DIR="."',
        '-DLIBDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libdir'))),
        '-DDATADIR="@0@"'.format(join_paths(get_option('prefix'), get_option('datadir'))),
        '-DLIBEXECDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libexecdir'))),
        '-DTESTDATADIR="@0@"'.format(test_data_dir),
        '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
        '-DLOCALSTATEDIR="@0@"'.format(local_state_dir),
        '-DSOURCEROOTDIR="@0@"'.format(source_root),
    ]
)

pk_test_e2e_exe = executable(
    'pk-test-e2e',
    'pk-test-e2e.c',
    dependencies: [
        packagekit_glib2_dep,
        glib_dep,
        gobject_dep,
        gio_dep,
        gio_unix_dep,
        config_dep,
    ],
    c_args: [
        '-DPK_COMPILATION=1',
        '-DG_LOG_DOMAIN="PackageKit"',
        '-DTESTDATADIR="@0@"'.format(test_data_dir),
    ],
    build_by_default: true,
    install: false,
)

# Integration test that drives a live packagekitd (dummy backend) over D-Bus. It
# needs the D-Bus/polkit policy installed to system paths, so we allow it to auto-SKIP
# in case those are not installed.
if get_option('daemon_tests')
    run_with_dbus = find_program('runner/run-ensure-dbus.py')
    run_daemon_test = find_program('runner/run-daemon-test.py')

    test('pk-test-daemon-internal',
        run_with_dbus,
        args: [pk_test_daemon_priv_exe],
        depends: [
            packagekit_test_py,
            pk_test_daemon_priv_exe,
        ],
        env: [
            'PYTHONPATH=@0@'.format(join_paths(build_root, 'lib', 'python')),
        ],
        suite: 'daemon',
        is_parallel: false,
        timeout: 360,
    )

    test('pk-test-e2e',
        run_daemon_test,
        args: [
          '--daemon', packagekitd_exec,
          '--test', pk_test_e2e_exe,
        ],
        depends: [
            packagekitd_exec,
            pk_test_e2e_exe,
            pk_backend_dummy
        ],
        suite: 'e2e',
        is_parallel: false,
        timeout: 480,
    )
endif
