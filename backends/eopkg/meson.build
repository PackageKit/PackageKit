# Backend for eopkg

install_data(
  'backend.conf',
  install_dir: join_paths(pk_backends_dir, 'eopkg'),
)
install_data(
  'eopkgBackend.py',
  install_dir: join_paths(pk_backends_dir, 'eopkg'),
  install_mode: 'rwxr-xr-x',
)

install_data(
  'groups.list',
  install_dir: join_paths(get_option('sysconfdir'), 'packagekit', 'eopkg.d')
)
