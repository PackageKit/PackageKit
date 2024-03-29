install_data(
  'pk-upgrade-distro.sh',
  install_dir: join_paths(get_option('datadir'), 'PackageKit'),
)

dbus_config_data = configuration_data()
dbus_config_data.set('PACKAGEKIT_USER', get_option('packagekit_user'))
dbus_config_data.set('libexecdir', join_paths(get_option('prefix'), get_option('libexecdir')))

dbus_sys_dir = get_option('dbus_sys')
if dbus_sys_dir == ''
  dbus_sys_dir = join_paths(get_option('datadir'), 'dbus-1', 'system.d')
endif

configure_file(
  input: 'org.freedesktop.PackageKit.conf.in',
  output: 'org.freedesktop.PackageKit.conf',
  configuration: dbus_config_data,
  install: true,
  install_dir: dbus_sys_dir,
)

dbus_services_dir = get_option('dbus_services')
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

  if get_option('offline_update')
    configure_file(
      input: 'packagekit-offline-update.service.in',
      output: 'packagekit-offline-update.service',
      configuration: dbus_config_data,
      install: true,
      install_dir: systemd_system_unit_dir,
    )

    if meson.version().version_compare('>=0.61')
        install_symlink(
            'packagekit-offline-update.service',
            pointing_to: '..' / 'packagekit-offline-update.service',
            install_dir: systemd_system_unit_dir / 'system-update.target.wants'
        )
    endif
  endif
endif

# Validate & install MetaInfo file
metainfo_file = files('org.freedesktop.packagekit.metainfo.xml')
install_data(
  metainfo_file,
  install_dir:join_paths(get_option('datadir'), 'metainfo'),
)
ascli_exe = find_program('appstreamcli', required: false)
if ascli_exe.found()
  test('validate metainfo file',
        ascli_exe,
        args: ['validate',
               '--no-net',
               '--pedantic',
               metainfo_file]
  )
endif

install_data(
  'transactions.db',
  install_dir: join_paths(get_option('localstatedir'), 'lib', 'PackageKit'),
)
