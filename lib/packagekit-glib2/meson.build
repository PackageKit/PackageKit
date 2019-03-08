packagekit_glib2_src_include = include_directories('.')
packagekit_glib2_includes = include_directories('.', '..')
packagekit_glib2_headers = files(
	'packagekit.h',
	'pk-bitfield.h',
	'pk-category.h',
	'pk-client.h',
	'pk-client-helper.h',
	'pk-client-sync.h',
	'pk-common.h',
	'pk-control.h',
	'pk-control-sync.h',
	'pk-desktop.h',
	'pk-details.h',
	'pk-distro-upgrade.h',
	'pk-enum.h',
	'pk-error.h',
	'pk-eula-required.h',
	'pk-files.h',
	'pk-media-change-required.h',
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
	'pk-transaction-past.h',
	'pk-transaction-list.h',
	'pk-update-detail.h',
)

install_headers(packagekit_glib2_headers, subdir : join_paths('PackageKit', 'packagekit-glib2'))

version_parts = meson.project_version().split('.')
version_configuration = configuration_data()
version_configuration.set('PK_MAJOR_VERSION', version_parts[0])
version_configuration.set('PK_MINOR_VERSION', version_parts[1])
version_configuration.set('PK_MICRO_VERSION', version_parts[2])

pk_version_header = configure_file(
	input: 'pk-version.h.in',
	output: 'pk-version.h',
	configuration: version_configuration,
)

packagekit_glib2_sources = files(
	'pk-bitfield.c',
	'pk-category.c',
	'pk-client.c',
	'pk-client-helper.c',
	'pk-client-sync.c',
	'pk-common.c',
	'pk-control.c',
	'pk-control-sync.c',
	'pk-debug.c',
	'pk-debug.h',
	'pk-desktop.c',
	'pk-details.c',
	'pk-distro-upgrade.c',
	'pk-enum.c',
	'pk-error.c',
	'pk-eula-required.c',
	'pk-files.c',
	'pk-media-change-required.c',
	'pk-item-progress.c',
	'pk-offline.c',
	'pk-offline-private.c',
	'pk-offline-private.h',
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
	'pk-transaction-past.c',
	'pk-transaction-list.c',
	'pk-update-detail.c',
	'pk-spawn-polkit-agent.c',
	'pk-spawn-polkit-agent.h',
)

pk_enum_type = gnome.mkenums(
	'pk-enum-types',
	c_template: 'pk-enum-types.c.template',
	h_template: 'pk-enum-types.h.template',
	sources: packagekit_glib2_headers,
	install_header: true,
	install_dir: join_paths(get_option('includedir'), 'PackageKit', 'packagekit-glib2')
)

packagekitprivate_library = static_library(
	'packagekitprivate',
	'packagekit-private.h',
	'pk-common-private.h',
	'pk-console-shared.c',
	'pk-console-shared.h',
	'pk-progress-bar.c',
	'pk-progress-bar.h',
	'pk-task-text.c',
	'pk-task-text.h',
	'pk-task-wrapper.c',
	'pk-task-wrapper.h',
	'pk-spawn-polkit-agent.h',
	dependencies: [
		glib_dep,
		gobject_dep,
		gio_dep,
		gio_unix_dep,
		config_dep,
	],
	include_directories: packagekit_glib2_includes,
	c_args: [
		'-DPK_COMPILATION=1',
		'-DG_LOG_DOMAIN="PackageKit"',
		'-DPACKAGE_LOCALE_DIR="@0@"'.format(package_locale_dir),
		'-DPACKAGE_DATA_DIR="@0@"'.format(package_data_dir),
		'-DTESTDATADIR="@0@"'.format(test_data_dir),
		'-DPK_DB_DIR="@0@"'.format(pk_db_dir),
		'-DLOCALSTATEDIR="@0@"'.format(local_state_dir),
	],
)

packagekit_glib2_library = library(
	'packagekit-glib2',
	packagekit_glib2_headers,
	pk_enum_type,
	pk_version_header,
	packagekit_glib2_sources,
	link_whole: packagekitprivate_library,
	dependencies: [
		glib_dep,
		gobject_dep,
		gio_dep,
		gio_unix_dep,
		config_dep,
	],
	include_directories: packagekit_glib2_includes,
	c_args: [
		'-DPK_COMPILATION=1',
		'-DG_LOG_DOMAIN="PackageKit"',
		'-DPACKAGE_LOCALE_DIR="@0@"'.format (package_locale_dir),
		'-DPACKAGE_DATA_DIR="@0@"'.format (package_data_dir),
		'-DTESTDATADIR="@0@"'.format(test_data_dir),
		'-DPK_DB_DIR="@0@"'.format(pk_db_dir),
		'-DLOCALSTATEDIR="@0@"'.format(local_state_dir),
	],
)

packagekit_glib2_dep = declare_dependency(
	link_with: packagekit_glib2_library,
	include_directories: packagekit_glib2_includes,
	sources: [
		packagekit_glib2_headers,
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

if get_option('gobject_introspection')
	packagekit_glib2_gir = gnome.generate_gir(
		packagekit_glib2_library,
		sources: packagekit_glib2_sources,
		namespace: 'PackageKitGlib',
		nsversion: '1.0',
		include_directories: packagekit_glib2_includes,
		includes: ['GObject-2.0', 'Gio-2.0', 'GLib-2.0'],
		header: 'packagekit-glib2/packagekit.h',
		identifier_prefix: 'Pk',
		symbol_prefix: 'pk',
		extra_args: [
			'-DPK_COMPILATION=1',
			'-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE',
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
		'packagekit-glib2',
		packages: ['glib-2.0', 'gio-2.0', 'gobject-2.0'],
		sources: packagekit_glib2_gir[0],
		install: true
	)
endif

pkg.generate(
	packagekit_glib2_library,
	filebase: 'packagekit-glib2',
	name: 'packagekit-glib2',
	description: 'PackageKit is a system daemon for installing stuff.',
	version: meson.project_version(),
	requires: [
		glib_dep,
		gobject_dep,
		gio_dep,
		gio_unix_dep,
	],
	subdirs: 'PackageKit'
)

executable(
	'pk-socket-example',
	'pk-socket-example.c',
	dependencies: [
		glib_dep,
		gobject_dep,
		gio_dep,
		gio_unix_dep,
	],
	build_by_default: true,
	install: false,
)

pk_test_private = executable(
	'pk-test-private',
	'pk-test-private.c',
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
		'-DPK_OFFLINE_DESTDIR="/tmp/PackageKit-self-test"',
		'-DTESTDATADIR="@0@"'.format(test_data_dir),
	],
	build_by_default: true,
	install: false,
)

pk_test_daemon = executable(
	'pk-test-daemon',
	'pk-test-daemon.c',
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

test(
	'pk-test-private',
	pk_test_private
)

test(
	'pk-test-daemon',
	pk_test_daemon
)
