add_languages('cpp')

curl_dep = meson.get_compiler('c').find_library('curl')
bzip2_dep = dependency('bzip2')

packagekit_backend_slack_module = shared_module(
  'pk_backend_slack',
  'pk-backend-slack.cc',
  'utils.cc',
  'pkgtools.cc',
  'slackpkg.cc',
  'dl.cc',
  'job.cc',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    curl_dep,
    gmodule_dep,
	sqlite3_dep,
	bzip2_dep,
  ],
  cpp_args: [
    '-DG_LOG_DOMAIN="PackageKit-Slackware"',
    '-DPK_COMPILATION=1',
    '-DLOCALSTATEDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('localstatedir'))),
    '-DLIBDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libdir'))),
    '-DSYSCONFDIR="@0@"'.format(get_option('sysconfdir')),
  ],
  override_options: ['c_std=c14', 'cpp_std=c++14'],
  install: true,
  install_dir: pk_plugin_dir,
)

subdir('tests')

slackware_config_data = configuration_data()
if (target_machine.cpu_family() == 'x86_64')
  slackware_config_data.set('pkgmain', 'slackware64')
else
  slackware_config_data.set('pkgmain', 'slackware')
endif

configure_file(
  input: 'Slackware.conf.in',
  output: 'Slackware.conf',
  configuration: slackware_config_data,
  install: true,
  install_dir: join_paths(get_option('sysconfdir'), 'PackageKit'),
)

install_data(
  'metadata.db',
  install_dir: join_paths(get_option('localstatedir'), 'cache', 'PackageKit', 'metadata'),
)
