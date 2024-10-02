appstream_dep = dependency('appstream', version: '>=0.14.0')
dnf_dep = dependency('libdnf', version: '>=0.43.1')
dnf5_dep = dependency('libdnf5').partial_dependency(includes: true, compile_args: true)
libdnf5_version = dnf5_dep.version().split('.')
rpm_dep = dependency('rpm')
sdbus_cpp_dep = dependency('sdbus-c++')
c_args = ['-DG_LOG_DOMAIN="PackageKit-DNF"']

if meson.get_compiler('c').has_function('hy_query_get_advisory_pkgs', prefix: '#include <libdnf/hy-query.h>', dependencies: dnf_dep)
   c_args += ['-DHAVE_HY_QUERY_GET_ADVISORY_PKGS']
endif

python = import('python')
python_exec = python.find_installation()
python_package_dir = get_option('pythonpackagedir')
if python_package_dir == ''
  python_package_dir = python_exec.get_install_dir()
endif
python_package_dir = join_paths(python_package_dir, 'dnf-plugins')

install_data(
  'notify_packagekit.py',
  install_dir: join_paths(python_package_dir),
)

add_languages('cpp')
shared_module(
  'notify_packagekit',
  'notify_packagekit.cpp',
  cpp_args: [
    '-std=c++20',
    '-DLIBDNF5_VERSION_MAJOR=' + libdnf5_version[0],
    '-DLIBDNF5_VERSION_MINOR=' + libdnf5_version[1],
    '-DLIBDNF5_VERSION_PATCH=' + libdnf5_version[2],
  ],
  include_directories: packagekit_glib2_includes,
  dependencies: [
    dnf5_dep,
    sdbus_cpp_dep,
  ],
  name_prefix: '',
  install: true,
  install_dir: get_option('libdir') / 'libdnf5/plugins/',
)

install_data(
  'notify_packagekit.conf',
  install_dir: get_option('sysconfdir') / 'dnf/libdnf5-plugins/'
)

shared_module(
  'pk_backend_dnf',
  'dnf-backend-vendor-@0@.c'.format(get_option('dnf_vendor')),
  'dnf-backend-vendor.h',
  'dnf-backend.c',
  'dnf-backend.h',
  'pk-backend-dnf-common.c',
  'pk-backend-dnf-common.h',
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
    c_args,
    '-DLIBEXECDIR="@0@"'.format(join_paths(get_option('prefix'), get_option('libexecdir'))),
  ],
  install: true,
  install_dir: pk_plugin_dir,
)

packagekit_refresh_repo_exec = executable(
  'packagekit-dnf-refresh-repo',
  '../../src/pk-shared.c',
  '../../src/pk-shared.h',
  'pk-backend-dnf-common.c',
  'pk-backend-dnf-common.h',
  'pk-backend-dnf-refresh.c',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    appstream_dep,
    dnf_dep,
    rpm_dep,
    gmodule_dep,
  ],
  install: true,
  install_dir: get_option('libexecdir'),
  c_args: [
    c_args
  ]
)
