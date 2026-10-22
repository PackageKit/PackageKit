# PackageKit shared library

libpackagekit_includes = include_directories('.')
libpackagekit_pub_headers = files(
    'packagekit.h',
    'pk-bitfield.h',
    'pk-client.h',
    'pk-client-sync.h',
    'pk-common.h',
    'pk-control.h',
    'pk-control-sync.h',
    'pk-details.h',
    'pk-system-upgrade.h',
    'pk-enum.h',
    'pk-error.h',
    'pk-issue.h',
    'pk-eula-required.h',
    'pk-files.h',
    'pk-item-progress.h',
    'pk-offline.h',
    'pk-package.h',
    'pk-package-id.h',
    'pk-package-ids.h',
    'pk-package-sack.h',
    'pk-package-sack-sync.h',
    'pk-progress.h',
    'pk-repo-detail.h',
    'pk-repo-signature-required.h',
    'pk-require-restart.h',
    'pk-results.h',
    'pk-source.h',
    'pk-task.h',
    'pk-task-sync.h',
    'pk-history-item.h',
    'pk-transaction-list.h',
    'pk-update-detail.h',
)

libpackagekit_src = files(
    'pk-bitfield.c',
    'pk-client.c',
    'pk-client-helper.c',
    'pk-client-helper-private.h',
    'pk-client-sync.c',
    'pk-common.c',
    'pk-control.c',
    'pk-control-sync.c',
    'pk-debug.c',
    'pk-debug.h',
    'pk-details.c',
    'pk-system-upgrade.c',
    'pk-enum.c',
    'pk-error.c',
    'pk-issue.c',
    'pk-eula-required.c',
    'pk-files.c',
    'pk-item-progress.c',
    'pk-offline.c',
    'pk-offline-private.c',
    'pk-offline-private.h',
    'pk-json-private.h',
    'pk-package.c',
    'pk-package-id.c',
    'pk-package-ids.c',
    'pk-package-sack.c',
    'pk-package-sack-sync.c',
    'pk-progress.c',
    'pk-repo-detail.c',
    'pk-repo-signature-required.c',
    'pk-require-restart.c',
    'pk-results.c',
    'pk-source.c',
    'pk-task.c',
    'pk-task-sync.c',
    'pk-history-item.c',
    'pk-transaction-list.c',
    'pk-update-detail.c',
    'pk-spawn-polkit-agent.c',
    'pk-spawn-polkit-agent.h',
)

libpackagekit_priv_src = files(
    'packagekit-private.h',
    'pk-common-private.h',
    'pk-console-private.c',
    'pk-console-private.h',
    'pk-progress-private.h',
    'pk-progress-bar.c',
    'pk-progress-bar.h',
    'pk-task-text.c',
    'pk-task-text.h',
    'pk-task-wrapper.c',
    'pk-task-wrapper.h',
    'pk-spawn-polkit-agent.h',
)

install_headers(libpackagekit_pub_headers, subdir : join_paths('packagekit', 'packagekit'))

version_parts = meson.project_version().split('.')
version_configuration = configuration_data()
version_configuration.set('PK_MAJOR_VERSION', version_parts[0])
version_configuration.set('PK_MINOR_VERSION', version_parts[1])
version_configuration.set('PK_MICRO_VERSION', version_parts[2])

pk_version_header = configure_file(
    input: 'pk-version.h.in',
    output: 'pk-version.h',
    configuration: version_configuration,
    install_dir: join_paths(get_option('includedir'), 'packagekit', 'packagekit'),
)

pk_enum_type = gnome.mkenums(
    'pk-enum-types',
    c_template: 'pk-enum-types.c.template',
    h_template: 'pk-enum-types.h.template',
    sources: libpackagekit_pub_headers,
    install_header: true,
    install_dir: join_paths(get_option('includedir'), 'packagekit', 'packagekit')
)

pkprivate_library = static_library(
    'packagekitprivate',
    libpackagekit_priv_src,
    dependencies: [
        glib_dep,
        gobject_dep,
        gio_dep,
        gio_unix_dep,
        config_dep,
    ],
    include_directories: libpackagekit_includes,
    c_args: [
        '-DPK_COMPILATION=1',
        '-DG_LOG_DOMAIN="PackageKit"',
        '-DPACKAGE_LOCALE_DIR="@0@"'.format(package_locale_dir),
        '-DPACKAGE_DATA_DIR="@0@"'.format(package_data_dir),
        '-DPK_DB_DIR="@0@"'.format(pk_db_dir),
    ],
)

libpackagekit_library = shared_library(
    'packagekit',
    pk_enum_type,
    pk_version_header,
    libpackagekit_src,
    link_whole: pkprivate_library,
    dependencies: [
        glib_dep,
        gobject_dep,
        gio_dep,
        gio_unix_dep,
        config_dep,
    ],
    include_directories: libpackagekit_includes,
    soversion: pk_api_level,
    version: meson.project_version(),
    c_args: [
        '-DPK_COMPILATION=1',
        '-DG_LOG_DOMAIN="PackageKit"',
        '-DPACKAGE_LOCALE_DIR="@0@"'.format (package_locale_dir),
        '-DPACKAGE_DATA_DIR="@0@"'.format (package_data_dir),
        '-DPK_DB_DIR="@0@"'.format(pk_db_dir),
    ],
    install: true,
)

libpackagekit_dep = declare_dependency(
    link_with: libpackagekit_library,
    include_directories: libpackagekit_includes,
    sources: [
        libpackagekit_pub_headers,
        pk_enum_type[1],
    ],
    dependencies: [
        glib_dep,
        gobject_dep,
        gio_dep,
        gio_unix_dep,
        config_dep,
    ],
)

if get_option('gir')
    libpackagekit_gir = gnome.generate_gir(
        libpackagekit_library,
        sources: [
            libpackagekit_src,
            libpackagekit_pub_headers,
            pk_enum_type,
            pk_version_header,
        ],
        namespace: 'PackageKit',
        nsversion: '1.0',
        include_directories: libpackagekit_includes,
        includes: ['GObject-2.0', 'Gio-2.0', 'GLib-2.0'],
        header: 'packagekit/packagekit.h',
        identifier_prefix: 'Pk',
        symbol_prefix: 'pk',
        export_packages: 'packagekit',
        extra_args: [
            '-DPK_COMPILATION=1',
        ],
        dependencies: [
            glib_dep,
            gobject_dep,
            gio_dep,
            gio_unix_dep,
            config_dep,
        ],
        install: true
    )

    gnome.generate_vapi(
        'packagekit',
        packages: ['glib-2.0', 'gio-2.0', 'gobject-2.0'],
        sources: libpackagekit_gir[0],
        install: true
    )
endif

pkg.generate(
    libpackagekit_library,
    filebase: 'packagekit',
    name: 'packagekit',
    description: 'PackageKit is a system daemon for installing stuff.',
    version: meson.project_version(),
    requires: [
        glib_dep,
        gobject_dep,
        gio_dep,
        gio_unix_dep,
    ],
    subdirs: 'packagekit'
)
