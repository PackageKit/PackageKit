shared_module(
  'pk_backend_entropy',
  'pk-backend-entropy.c',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    gmodule_dep,
  ],
  c_args: [
    '-DG_LOG_DOMAIN="PackageKit-Entropy"',
  ],
  install: true,
  install_dir: pk_plugin_dir,
)

install_data(
  'entropyBackend.py',
  install_dir: join_paths(get_option('datadir'), 'PackageKit', 'helpers', 'entropy')
  install_mode: 'rwxr--r--'
)
