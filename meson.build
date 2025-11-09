project('PackageKit', 'c',
  meson_version : '>=1.0',
  default_options : ['warning_level=2', 'c_std=gnu17'],

  license : 'LGPL-2.1+',
  version : '1.3.3',
)

gnome = import('gnome')
i18n = import('i18n')
pkg = import('pkgconfig')

source_root = meson.project_source_root()

glib_req_version = '2.76'
glib_req = '>= @0@'.format(glib_req_version)
glib_major = glib_req_version.split('.')[0].to_int()
glib_minor = glib_req_version.split('.')[1].to_int()

glib_dep = dependency('glib-2.0', version: glib_req)
gobject_dep = dependency('gobject-2.0', version: glib_req)
gio_dep = dependency('gio-2.0', version: glib_req)
gio_unix_dep = dependency('gio-unix-2.0', version: glib_req)
gmodule_dep = dependency('gmodule-2.0', version: glib_req)
sqlite3_dep = dependency('sqlite3')
polkit_dep = dependency('polkit-gobject-1', version: '>=0.114')
ply_client_dep = dependency('ply-boot-client', version: '>=0.9.5', required: false)

libsystemd = []
if get_option('systemd')
  libsystemd = dependency('libsystemd', version: '>=213')
  systemd_system_unit_dir = get_option('systemdsystemunitdir')
  systemd_user_unit_dir = get_option('systemduserunitdir')
  if systemd_system_unit_dir == '' or systemd_user_unit_dir == ''
    systemd = dependency('systemd')
  endif
  if systemd_system_unit_dir == ''
    systemd_system_unit_dir = systemd.get_variable(pkgconfig: 'systemdsystemunitdir')
  endif
  if systemd_user_unit_dir == ''
    systemd_user_unit_dir = systemd.get_variable(pkgconfig: 'systemduserunitdir')
  endif

  add_project_arguments ('-DHAVE_SYSTEMD_SD_DAEMON_H=1', language: 'c')
  add_project_arguments ('-DHAVE_SYSTEMD_SD_LOGIN_H=1', language: 'c')
  add_project_arguments ('-DHAVE_SYSTEMD_SD_JOURNAL_H=1', language: 'c')
endif

elogind = []
if get_option('elogind')
  elogind = dependency('elogind', version: '>=229.4')
  add_project_arguments ('-DHAVE_SYSTEMD_SD_LOGIN_H=1', language: 'c')
  add_project_arguments ('-DHAVE_SYSTEMD_SD_JOURNAL_H=1', language: 'c')
endif

if get_option('local_checkout')
  add_project_arguments ('-DPK_BUILD_LOCAL=1', language: 'c')
endif

if get_option('daemon_tests')
  add_project_arguments ('-DPK_ENABLE_DAEMON_TESTS=1', language: 'c')
endif

# Ensure functions like realpath(3) and other "default" functions are available
add_project_arguments ('-D_DEFAULT_SOURCE', language: 'c')

add_project_arguments (
  '-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_@0@_@1@'.format(glib_major, glib_minor),
  language: 'c'
)

# allow the daemon to include library files directly
add_project_arguments ('-DPK_COMPILATION', language: ['c', 'cpp'])

conf = configuration_data()
conf.set_quoted('DATADIR', join_paths(get_option('prefix'), get_option('datadir')))
conf.set_quoted('LIBDIR', join_paths(get_option('prefix'), get_option('libdir')))
conf.set_quoted('SYSCONFDIR', get_option('sysconfdir'))

conf.set_quoted('PROJECT_NAME', meson.project_name())
conf.set_quoted('PROJECT_VERSION', meson.project_version())

cc = meson.get_compiler('c')
if cc.has_function('setpriority')
  conf.set('HAVE_SETPRIORITY', '1')
endif
if cc.has_function('clearenv')
  conf.set('HAVE_CLEARENV', '1')
endif
if cc.has_header('unistd.h')
  conf.set('HAVE_UNISTD_H', '1')
endif

config_header = configure_file(
  output: 'config.h',
  configuration: conf,
)

config_dep = declare_dependency(
  sources: config_header,
  include_directories: include_directories('.')
)

pk_db_dir = join_paths(get_option('localstatedir'), 'lib', 'PackageKit')
local_state_dir = get_option('localstatedir')
test_data_dir = join_paths(source_root, 'tests', 'data')
package_data_dir = get_option('datadir')
package_locale_dir = join_paths(get_option('prefix'), get_option('datadir'), 'locale')
pk_plugin_dir = join_paths(get_option('prefix'), get_option('libdir'), 'packagekit-backend')

# enable strict checks and make warnings errors in maintainer mode
if get_option('maintainer')
  add_project_arguments(
    cc.get_supported_arguments([
            '-Werror',
            '-Wall',
            '-Wextra',
            '-Wcast-align',
            '-Wno-uninitialized',
            '-Wempty-body',
            '-Winit-self',
            '-Wnull-dereference',
            '-Wfloat-equal',
            '-Winline',
            '-Wno-error=comment',
        ], checked: 'warn'),
        language: ['c', 'cpp']
    )
endif

# default compiler flags for C and C++
# if a common compiler flag is not supported by C, then it likely
# isn't by C++ either. So, we don't need a double check here.
add_project_arguments(
    cc.get_supported_arguments([
        '-Werror=missing-include-dirs',
        '-Wno-strict-aliasing',
        '-Winline',
        '-Wno-unused-parameter',
        '-Wno-error=unused-parameter',
    ]),
    language: ['c', 'cpp']
)

# default compiler warning/error flags for C only
add_project_arguments(
  cc.get_supported_arguments([
    '-Wcast-align',
    '-Wno-uninitialized',
    '-Werror=missing-declarations',
    '-Wredundant-decls',
    '-Wpointer-arith',
    '-Wcast-align',
    '-Wwrite-strings',
    '-Winit-self',
    '-Wreturn-type',
    '-Wformat-nonliteral',
    '-Wformat-security',
    '-Wmissing-include-dirs',
    '-Wmissing-format-attribute',
    '-Wclobbered',
    '-Wempty-body',
    '-Wignored-qualifiers',
    '-Wsign-compare',
    '-Wtype-limits',
    '-Wuninitialized',
    '-Wmissing-parameter-type',
    '-Woverride-init',
    '-Wno-unused-parameter',
    '-Wno-error=unused-parameter',
    '-Werror=misleading-indentation',
    '-Werror=return-type',
    '-Werror=format-security',
    '-Wformat',
    '-Werror=shadow',
    '-Werror=missing-declarations',
    '-Werror=pointer-arith',
    '-Werror=strict-prototypes',
    '-Werror=missing-prototypes',
    '-Werror=implicit-function-declaration',
    '-Werror=int-conversion',
    '-Werror=incompatible-pointer-types',
    '-Werror=declaration-after-statement',
    '-Werror=empty-body',
  ]),
  language: 'c'
)

# default warning flags for C++ only
add_project_arguments(
  '-Werror=init-self',
  language: 'cpp'
)

subdir('po')
subdir('policy')
subdir('etc')
subdir('data')
subdir('lib')
subdir('src')
subdir('client')
subdir('backends')
subdir('contrib')
subdir('docs')
