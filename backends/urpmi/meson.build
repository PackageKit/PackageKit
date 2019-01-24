shared_module(
  'pk_backend_urpmi',
  'pk-backend-urpmi.c',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    gmodule_dep,
  ],
  c_args: [
    '-DPK_COMPILATION',
    '-DG_LOG_DOMAIN="PackageKit-Urpmi"',
  ],
  install: true,
  install_dir: pk_plugin_dir,
)

subdir('helpers')
