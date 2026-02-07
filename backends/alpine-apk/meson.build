libapk = dependency('apk', version: '>=3.0.0')

alpine_apk_backend_files = files(
  'pk-backend-apk.c',
  'pk-apk-query.c',
  'backend-apk-private.c',
)

shared_module(
  'pk_backend_alpine_apk',
  alpine_apk_backend_files,
  include_directories: packagekit_src_include,
  dependencies: [
    libapk,
    packagekit_glib2_dep,
    gmodule_dep,
  ],
  c_args: [
    '-DG_LOG_DOMAIN="PackageKit-AlpineApk"',
  ],
  install: true,
  install_dir: pk_plugin_dir,
)
