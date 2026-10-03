libapk = dependency('apk', version: '>=3.0.0')

apk_backend_files = files(
  'pk-backend-apk.c',
  'pk-apk-query.c',
  'pk-apk-convert.c',
  'pk-apk-open.c',
)

# TODO: add chimera
# https://github.com/chimera-linux/cports/blob/master/Packaging.md#naming
apk_vendor = get_option('apk_vendor')
if apk_vendor == 'postmarketOS' or apk_vendor == 'alpine'
  apk_backend_files += configure_file(
    input: 'matches_alpine_pmos.inc',
    output: 'matches_inc.h',
    copy: true,
  )
else
  warning('no known apk_vendor selected')
  apk_backend_files += configure_file(
    configuration: [],
    output: 'matches_inc.h',
    copy: true,
  )
endif

shared_module(
  'pk_backend_apk',
  apk_backend_files,
  include_directories: packagekit_src_include,
  dependencies: [
    libapk,
    packagekit_glib2_dep,
    gmodule_dep,
  ],
  c_args: [
    '-DG_LOG_DOMAIN="PackageKit-Apk"',
  ],
  install: true,
  install_dir: pk_plugin_dir,
  override_options : ['c_std=gnu23'],
)
