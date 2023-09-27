shared_module(
  'pk_backend_pisi',
  'pk-backend-pisi.c',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    gmodule_dep,
  ],
  c_args: [
    '-DPK_COMPILATION',
    '-DG_LOG_DOMAIN="PackageKit-Pisi"',
  ],
  install: true,
  install_dir: pk_plugin_dir,
)

install_data(
  'pisiBackend.py',
  install_dir: join_paths(get_option('datadir'), 'PackageKit', 'helpers', 'pisi'),
  install_mode: 'rwxr--r--',
)
