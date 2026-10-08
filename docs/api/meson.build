subdir('dbus')

gidocgen_config_data = configuration_data()
gidocgen_config_data.set('VERSION', meson.project_version())
gidocgen_toml = configure_file(
  input: 'PackageKitGlib.toml.in',
  output: 'PackageKitGlib.toml',
  configuration: gidocgen_config_data,
)

custom_target(
  'api-docs',
  input: [gidocgen_toml, packagekit_glib2_gir[0]],
  output: 'packagekit',
  command: [
    find_program('gi-docgen'),
    'generate',
    '--quiet',
    '--no-namespace-dir',
    '--config=@INPUT0@',
    '--content-dir=' + join_paths(meson.current_source_dir(), 'spec'),
    '--content-dir=' + join_paths(meson.current_build_dir(), 'dbus'),
    '--output-dir=@OUTPUT@',
    '@INPUT1@',
  ],
  depends: packagekit_dbus_md,
  depend_files: files(
    'spec/backend-compiled.md',
    'spec/backend-spawn.md',
    'spec/concepts.md',
    'spec/introduction.md',
    'spec/offline-updates.md',
    'spec/provides-component-naming.md',
    'spec/security.md',
    'spec/setting-the-proxy.md',
  ),
  build_by_default: true,
  install: true,
  install_dir: join_paths(get_option('datadir'), 'doc'),
)
