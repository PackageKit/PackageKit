subdir('helpers')

shared_module(
  'pk_backend_test_fail',
  'pk-backend-test-fail.c',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    gmodule_dep,
  ],
  c_args: [
    '-DG_LOG_DOMAIN="PackageKit-Test"',
  ],
  install: true,
  install_dir: pk_plugin_dir,
)

shared_module(
  'pk_backend_test_nop',
  'pk-backend-test-nop.c',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    gmodule_dep,
  ],
  c_args: [
    '-DG_LOG_DOMAIN="PackageKit-Test"',
  ],
  install: true,
  install_dir: pk_plugin_dir,
)

shared_module(
  'pk_backend_test_spawn',
  'pk-backend-test-spawn.c',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    gmodule_dep,
  ],
  c_args: [
    '-DG_LOG_DOMAIN="PackageKit-Test"',
  ],
  install: true,
  install_dir: pk_plugin_dir,
)

shared_module(
  'pk_backend_test_succeed',
  'pk-backend-test-succeed.c',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    gmodule_dep,
  ],
  c_args: [
    '-DG_LOG_DOMAIN="PackageKit-Test"',
  ],
  install: true,
  install_dir: pk_plugin_dir,
)

shared_module(
  'pk_backend_test_thread',
  'pk-backend-test-thread.c',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    gmodule_dep,
  ],
  c_args: [
    '-DG_LOG_DOMAIN="PackageKit-Test"',
  ],
  install: true,
  install_dir: pk_plugin_dir,
)
