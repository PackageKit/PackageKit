bzip2_dep = dependency('bzip2')

packagekit_backend_slack_test1 = executable('prog',
  'dl-test.cc',
  shared_sources,
  link_with: [
    packagekit_backend_slack_module
  ],
  include_directories: [
    include_directories('..'),
    packagekit_src_include,
  ],
  dependencies: [
    packagekit_glib2_dep,
    gmodule_dep,
    sqlite3_dep,
    bzip2_dep,
    polkit_dep
  ],
  cpp_args: [
    '-DG_LOG_DOMAIN="PackageKit-Slackware"',
    '-DPK_COMPILATION=1',
    '-DLOCALSTATEDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('localstatedir'))),
    '-DLIBDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libdir'))),
    '-DSYSCONFDIR="@0@"'.format(get_option('sysconfdir')),
    '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
    '-DLIBEXECDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libexecdir'))),
    '-DPK_DB_DIR="."',
  ],
  c_args: [
    '-DG_LOG_DOMAIN="PackageKit-Slackware"',
    '-DPK_COMPILATION=1',
    '-DLOCALSTATEDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('localstatedir'))),
    '-DLIBDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libdir'))),
    '-DSYSCONFDIR="@0@"'.format(get_option('sysconfdir')),
    '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
    '-DLIBEXECDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libexecdir'))),
    '-DPK_DB_DIR="."',
  ]
)

test('dl-test', packagekit_backend_slack_test1)
