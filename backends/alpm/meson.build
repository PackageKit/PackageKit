alpm_dep = dependency('libalpm', version: '>=10.0.0')

shared_module(
  'pk_backend_alpm',
  'pk-backend-alpm.c',
  'pk-backend-alpm.h',
  'pk-alpm-config.c',
  'pk-alpm-config.h',
  'pk-alpm-databases.c',
  'pk-alpm-databases.h',
  'pk-alpm-depends.c',
  'pk-alpm-environment.c',
  'pk-alpm-environment.h',
  'pk-alpm-error.c',
  'pk-alpm-error.h',
  'pk-alpm-groups.c',
  'pk-alpm-groups.h',
  'pk-alpm-install.c',
  'pk-alpm-packages.c',
  'pk-alpm-packages.h',
  'pk-alpm-remove.c',
  'pk-alpm-search.c',
  'pk-alpm-sync.c',
  'pk-alpm-transaction.c',
  'pk-alpm-transaction.h',
  'pk-alpm-update.c',
  include_directories: packagekit_src_include,
  dependencies: [
    packagekit_glib2_dep,
    alpm_dep,
    gmodule_dep,
  ],
  c_args: [
    '-DPK_LOG_PREFIX="PACKAGEKIT"'
    '-DG_LOG_DOMAIN="PackageKit-alpm"',
    '-DPK_BACKEND_CONFIG_FILE="@0@"'.format(join_paths(get_option('confdir'), 'pacman.conf')),
    '-DPK_BACKEND_GROUP_FILE="@0@"'.format(join_paths(get_option('confdir'), 'groups.list')),
    '-DPK_BACKEND_REPO_FILE="@0@"'.format(join_paths(get_option('confdir'), 'repos.list')),
    '-DPK_BACKEND_DEFAULT_PATH="/bin:/usr/bin:/sbin:/usr/sbin"',
  ],
  install: true,
  install_dir: pk_plugin_dir,
)

install_data(
  'groups.list'
  'pacman.conf'
  'repos.list',
  install_dir: join_paths(get_option('sysconfdir'), 'PackageKit', 'alpm.d')
)
