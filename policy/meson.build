intltool_merge = find_program('intltool-merge')
custom_target(
  'org.freedesktop.packagekit.policy',
  input: 'org.freedesktop.packagekit.policy.in',
  output: 'org.freedesktop.packagekit.policy',
  command: [
    intltool_merge,
    '--xml-style',
    '--utf8',
    join_paths(meson.source_root(), 'po'),
    '@INPUT@',
    '@OUTPUT@',
  ],
  install: true,
  install_dir: join_paths(get_option('datadir'), 'polkit-1', 'actions')
)

install_data(
  'org.freedesktop.packagekit.rules',
  install_dir: join_paths(get_option('datadir'), 'polkit-1', 'rules.d')
)
