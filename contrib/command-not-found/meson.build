executable(
  'pk-command-not-found',
  'pk-command-not-found.c',
  dependencies: packagekit_glib2_dep,
  install: true,
  install_dir: get_option('libexecdir'),
  c_args: [
    '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
    '-DG_LOG_DOMAIN="PackageKit"',
    '-DPACKAGE_LOCALE_DIR="@0@"'.format(get_option('localedir')),
    '-DSYSCONFDIR="@0@"'.format(get_option('sysconfdir')),
  ]
)

bashprofile_config_data = configuration_data()
bashprofile_config_data.set('LIBEXECDIR', get_option('libexecdir'))
configure_file(
  input: 'PackageKit.sh.in',
  output: 'PackageKit.sh',
  configuration: bashprofile_config_data,
  install: true,
  install_dir: join_paths(get_option('sysconfdir'), 'profile.d'),
)

install_data(
  'CommandNotFound.conf',
  install_dir: join_paths(get_option('sysconfdir'), 'PackageKit'),
)
