add_languages('cpp')

cpp_compiler = meson.get_compiler('cpp')

gstreamer_dep = dependency('gstreamer-1.0')
gstreamer_base_dep = dependency('gstreamer-base-1.0')
gstreamer_plugins_base_dep = dependency('gstreamer-plugins-base-1.0')
appstream_dep = dependency('appstream', version: '>=0.16.0')
apt_pkg_dep = dependency('apt-pkg', version: '>=1.9.2')

# Check whether apt supports ddtp
ddtp_flag = []
if cpp_compiler.compiles(
  '''
    #include <apt-pkg/pkgcache.h>
    int main () {
      pkgCache::DescIterator d;
      return 0;
    }
  ''',
  dependencies: [
    apt_pkg_dep
  ]
)
  ddtp_flag = ['-DHAVE_DDTP']
endif

c_args = ['-DG_LOG_DOMAIN="PackageKit-APT"',
          '-DDATADIR="@0@"'.format(join_paths(get_option('prefix'), get_option('datadir'))),
]

shared_module(
  'pk_backend_apt',
  'pk-backend-apt.cpp',
  'acqpkitstatus.cpp',
  'acqpkitstatus.h',
  'apt-cache-file.cpp',
  'apt-cache-file.h',
  'apt-job.cpp',
  'apt-job.h',
  'apt-messages.cpp',
  'apt-messages.h',
  'apt-sourceslist.cpp',
  'apt-sourceslist.h',
  'apt-utils.cpp',
  'apt-utils.h',
  'deb-file.cpp',
  'deb-file.h',
  'gst-matcher.cpp',
  'gst-matcher.h',
  'pkg-list.cpp',
  'pkg-list.h',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    gmodule_dep,
    apt_pkg_dep,
    appstream_dep,
    gstreamer_dep,
    gstreamer_base_dep,
    gstreamer_plugins_base_dep,
  ],
  c_args: c_args,
  cpp_args: [
    '-DPK_COMPILATION',
    c_args,
    ddtp_flag,
  ],
  link_args: [
    '-lutil',
  ],
  override_options: [
    'c_std=c11',
    'cpp_std=c++17'
  ],
  install: true,
  install_dir: pk_plugin_dir,
)

install_data(
  '20packagekit',
  install_dir: join_paths(get_option('sysconfdir'), 'apt', 'apt.conf.d'),
)

install_data(
  'pkconffile.nodiff',
  install_dir: join_paths(get_option('datadir'), 'PackageKit', 'helpers', 'apt'),
)

if get_option('systemd')
  executable(
    'pk-debconf-helper',
    'pk-debconf-helper.c',
    dependencies: [
      packagekit_glib2_dep,
      libsystemd
    ],
    install: true,
    install_dir: get_option('libexecdir')
  )

  sd_config_data = configuration_data()
  sd_config_data.set('libexecdir', join_paths(get_option('prefix'), get_option('libexecdir')))

  configure_file(
    input: 'pk-debconf-helper.service.in',
    output: 'pk-debconf-helper.service',
    configuration: sd_config_data,
    install: true,
    install_dir: systemd_user_unit_dir,
  )
  configure_file(
    input: 'pk-debconf-helper.socket.in',
    output: 'pk-debconf-helper.socket',
    configuration: sd_config_data,
    install: true,
    install_dir: systemd_user_unit_dir,
  )
endif
