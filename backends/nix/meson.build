add_languages('cpp', native: false)

nix_expr_dep = dependency('nix-expr', version: '>=2.9')
nix_main_dep = dependency('nix-main', version: '>=2.9')
nix_store_dep = dependency('nix-store', version: '>=2.9')
nix_cmd_dep = dependency('nix-cmd', version: '>=2.9')

shared_module(
  'pk_backend_nix',
  'pk-backend-nix.cc',
  'nix-lib-plus.cc',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    nix_expr_dep,
    nix_main_dep,
    nix_cmd_dep,
    nix_store_dep,
    gmodule_dep,
  ],
  cpp_args: [
    '-DG_LOG_DOMAIN="PackageKit-Nix"',
  ],
  install: true,
  install_dir: pk_plugin_dir,
)
