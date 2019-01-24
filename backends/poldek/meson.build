cc = meson.get_compiler('c')

poldek_dep = cc.find_library('poldek')
poclidek_dep = cc.find_library('poclidek')

shared_module(
  'pk_backend_poldek',
  'pk-backend-poldek.c',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    poldek_dep,
    poclidek_dep,
    gmodule_dep,
  ],
  c_args: [
    '-DG_LOG_DOMAIN="PackageKit-Poldek"',
    '-I/usr/include/poldek'
  ],
  install: true,
  install_dir: pk_plugin_dir,
)
