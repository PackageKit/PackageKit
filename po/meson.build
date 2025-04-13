i18n.gettext(
  meson.project_name(),
  preset: 'glib',
  args: [ '--keyword=Description' ]
)

run_target('fix-translations',
  command: [
    join_paths(source_root, 'contrib/fix_translations.py'),
    join_paths(source_root, 'po')
  ]
)
