install_data('org.freedesktop.packagekit.rules',
             install_dir : join_paths(get_option('datadir'), 'polkit-1', 'rules.d'))
i18n.merge_file(
  input: 'org.freedesktop.packagekit.policy.in',
  output: 'org.freedesktop.packagekit.policy',
  install: true,
  install_dir: join_paths(get_option('datadir'), 'polkit-1', 'actions') ,
  type: 'xml',
  po_dir: join_paths(meson.source_root(), 'po')
)
