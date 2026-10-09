# The generic module for spawned backends

pk_backend_modules += shared_module(
  'pk_backend_spawn',
  'pk-backend-spawn.c',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    gmodule_dep,
    config_dep,
  ],
  c_args: [
    '-DG_LOG_DOMAIN="PackageKit-Spawn"',
  ],
  install: true,
  install_dir: pk_backends_dir,
)
