shared_module(
  'pk_backend_portage',
  'pk-backend-portage.c',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    gmodule_dep,
  ],
  c_args: [
    '-DG_LOG_DOMAIN="PackageKit-Portage"',
  ],
  install: true,
  install_dir: pk_plugin_dir,
)

install_data(
  'portageBackend.py',
  install_dir: join_paths(get_option('datadir'), 'PackageKit', 'helpers', 'portage')
  install_mode: 'rwxr--r--'
)
