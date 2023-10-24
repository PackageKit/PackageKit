add_languages('cpp')

libpkg_dep = dependency('pkg', version : '>=1.20.99')

c_args = [
    '-DG_LOG_DOMAIN="PackageKit-FreeBSD"',
  ]

shared_module(
  'pk_backend_freebsd',
  sources: [
    'pk-backend-freebsd.cpp'
  ],
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    gmodule_dep,
    libpkg_dep
  ],
  override_options: [
    'cpp_std=c++17'
  ],
  c_args: c_args,
  cpp_args: c_args,
  install: true,
  install_dir: pk_plugin_dir,
)
