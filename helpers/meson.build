packagekit_src_include = include_directories('.')

helper_exec = executable(
  'dependency-solving-helper',
  'dependency-solving-helper.cpp',
  dependencies: [
    dependency('libbonsole'),
    dependency('dbus-1'),
    dependency('libxslt'),
  ],
  install: true,
  install_dir: get_option('libexecdir'),
  cxx_args: [
    '-DLIBDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libdir'))),
    '-DSYSCONFDIR="@0@"'.format(get_option('sysconfdir')),
    '-DVERSION="@0@"'.format(meson.project_version()),
    '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
    '-DPACKAGE_LOCALE_DIR="@0@"'.format(package_locale_dir),
    '-DLIBEXECDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libexecdir'))),
                                    
  ]
)
