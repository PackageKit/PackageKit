install_data(
	'pk-upgrade-distro.sh',
	install_dir: join_paths(get_option('datadir'), 'PackageKit'),
)

dbus_config_data = configuration_data()
dbus_config_data.set('PACKAGEKIT_USER', get_option('packagekit-user'))
dbus_config_data.set('libexecdir', join_paths(get_option('prefix'), get_option('libexecdir')))

dbus_sys_dir = get_option('dbus-sys')
if dbus_sys_dir == ''
	dbus_sys_dir = join_paths(get_option('sysconfdir'), 'dbus-1', 'system.d')
endif

configure_file(
	input: 'org.freedesktop.PackageKit.conf.in',
	output: 'org.freedesktop.PackageKit.conf',
	configuration: dbus_config_data,
	install: true,
	install_dir: dbus_sys_dir,
)

dbus_services_dir = get_option('dbus-services')
if dbus_services_dir == ''
	dbus_services_dir = join_paths(get_option('datadir'), 'dbus-1', 'system-services')
endif

configure_file(
	input: 'org.freedesktop.PackageKit.service.in',
	output: 'org.freedesktop.PackageKit.service',
	configuration: dbus_config_data,
	install: true,
	install_dir: dbus_services_dir,
)


if get_option('systemd')
	configure_file(
		input: 'packagekit.service.in',
		output: 'packagekit.service',
		configuration: dbus_config_data,
		install: true,
		install_dir: systemd_system_unit_dir,
	)

	if get_option('offline-update')
		configure_file(
			input: 'packagekit-offline-update.service.in',
			output: 'packagekit-offline-update.service',
			configuration: dbus_config_data,
			install: true,
			install_dir: systemd_system_unit_dir,
		)
	endif
endif

install_data(
	'transactions.db',
	install_dir: join_paths(get_option('localstatedir'), 'lib', 'PackageKit'),
)

subdir('tests')
