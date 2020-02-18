bzip2_dep = dependency('bzip2')

pk_slack_test_dependencies = [
  packagekit_glib2_dep,
  gmodule_dep,
  sqlite3_dep,
  bzip2_dep,
  polkit_dep
]

pk_slack_test_cpp_args = [
  '-DG_LOG_DOMAIN="PackageKit-Slackware"',
  '-DPK_COMPILATION=1',
  '-DLOCALSTATEDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('localstatedir'))),
  '-DLIBDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libdir'))),
  '-DSYSCONFDIR="@0@"'.format(get_option('sysconfdir')),
  '-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
  '-DLIBEXECDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libexecdir'))),
  '-DPK_DB_DIR="."',
]

pk_slack_test_include_directories = [
  include_directories('..'),
  packagekit_src_include,
]

pk_slack_test_dl = executable('pk-slack-test-dl',
  ['dl-test.cc', 'definitions.cc'],
  link_with: packagekit_backend_slack_module,
  include_directories: pk_slack_test_include_directories,
  dependencies: pk_slack_test_dependencies,
  cpp_args: pk_slack_test_cpp_args,
  c_args: pk_slack_test_cpp_args
)

pk_slack_test_slackpkg = executable('pk-slack-test-slackpkg',
  ['slackpkg-test.cc', 'definitions.cc'],
  link_with: packagekit_backend_slack_module,
  include_directories: pk_slack_test_include_directories,
  dependencies: pk_slack_test_dependencies,
  cpp_args: pk_slack_test_cpp_args,
  c_args: pk_slack_test_cpp_args
)

pk_slack_test_job = executable('pk-slack-test-job',
  ['job-test.cc', 'definitions.cc'],
  link_with: packagekit_backend_slack_module,
  include_directories: pk_slack_test_include_directories,
  dependencies: pk_slack_test_dependencies,
  cpp_args: pk_slack_test_cpp_args,
  c_args: pk_slack_test_cpp_args
)

test('slack-dl', pk_slack_test_dl)
test('slac-slackpkg', pk_slack_test_slackpkg)
test('slack-job', pk_slack_test_job)
