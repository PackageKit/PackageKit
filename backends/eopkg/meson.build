shared_module(
  'pk_backend_eopkg',
  'pk-backend-eopkg.c',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    gmodule_dep,
  ],
  c_args: [
    '-DPK_COMPILATION',
    '-DG_LOG_DOMAIN="PackageKit-Eopkg"',
  ],
  install: true,
  install_dir: pk_plugin_dir,
)

install_data(
  'eopkgBackend.py',
  install_dir: join_paths(get_option('datadir'), 'PackageKit', 'helpers', 'eopkg'),
  install_mode: 'rwxr--r--',
)

install_data(
  'groups.list',
  install_dir: join_paths(get_option('sysconfdir'), 'PackageKit', 'eopkg.d')
)
