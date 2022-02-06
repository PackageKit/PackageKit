appstream_dep = dependency('appstream-glib')
dnf_dep = dependency('libdnf', version: '>=0.43.1')
rpm_dep = dependency('rpm')
c_args = ['-DG_LOG_DOMAIN="PackageKit-DNF"']

if meson.get_compiler('c').has_function('hy_query_get_advisory_pkgs', prefix: '#include <libdnf/hy-query.h>', dependencies: dnf_dep)
   c_args += ['-DHAVE_HY_QUERY_GET_ADVISORY_PKGS']
endif

shared_module(
  'pk_backend_dnf',
  'dnf-backend-vendor-@0@.c'.format(get_option('dnf_vendor')),
  'dnf-backend-vendor.h',
  'dnf-backend.c',
  'dnf-backend.h',
  'pk-backend-dnf.c',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    appstream_dep,
    dnf_dep,
    rpm_dep,
    gmodule_dep,
  ],
  c_args: [
    c_args
  ],
  install: true,
  install_dir: pk_plugin_dir,
)
