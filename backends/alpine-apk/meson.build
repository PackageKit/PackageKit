apk_client_dep = dependency('apk-polkit-client-2', version: '>=3.0.1')
libapk = dependency('apk', version: '>=3.0.0')

shared_module(
  'pk_backend_alpine_apk',
  'pk-backend-alpine-apk.c',
  include_directories: packagekit_src_include,
  dependencies: [
    apk_client_dep,
    packagekit_glib2_dep,
    gmodule_dep,
  ],
  c_args: [
    '-DG_LOG_DOMAIN="PackageKit-AlpineApk"',
  ],
  # that's really weird
  link_args: [
    '-lapk', '-lapk-polkit-client2',
  ],
  install: true,
  install_dir: pk_plugin_dir,
)
