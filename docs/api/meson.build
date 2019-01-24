subdir('dbus')

version_xml_config_data = configuration_data()
version_xml_config_data.set('VERSION', meson.project_version())
version_xml = configure_file(
  input: 'version.xml.in',
  output: 'version.xml',
  configuration: version_xml_config_data,
)

gnome.gtkdoc(
  'PackageKit',
  main_sgml: 'PackageKit-docs.sgml',
  src_dir: packagekit_glib2_src_include,
  include_directories: packagekit_glib2_includes,
  ignore_headers: [
    'config.h',
    'pk-marshal.h',
    'pk-common-private.h',
    'pk-debug.h',
    'pk-offline-private.h',
    'pk-spawn-polkit-agent.h',
  ],
  dependencies: [
    glib_dep,
    gobject_dep,
    gio_dep,
    gio_unix_dep,
    config_dep,
  ],
  html_assets: [
    'spec/pk-structure.png',
    'spec/pk-structure.svg',
    'spec/pk-transactions.svg',
    'spec/pk-transactions-set-locale.png',
    'spec/pk-transactions-repair-required.png',
    'spec/pk-transactions-failure.png',
    'spec/pk-transactions-download.png',
    'spec/pk-transactions-sig-install.png',
    'spec/pk-transactions-success.png',
    'spec/pk-transactions-auto-untrusted.png',
    'spec/pk-transactions-trusted.png',
  ],
  content_files: [
    version_xml,
    'spec/pk-backend-compiled.xml',
    'spec/pk-backend-spawn.xml',
    'spec/pk-concepts.xml',
    'spec/pk-introduction.xml',
    packagekit_ref_xml,
    packagekit_transaction_ref_xml,
  ],
  install: true,
)
