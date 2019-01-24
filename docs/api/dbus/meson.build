spec_to_docbook = find_program('spec-to-docbook.sh')

packagekit_ref_xml = custom_target(
  'org.freedesktop.PackageKit.ref.xml',
  input: join_paths(meson.source_root(), 'src', 'org.freedesktop.PackageKit.xml'),
  output: 'org.freedesktop.PackageKit.ref.xml',
  build_by_default: true,
  command: [
    spec_to_docbook,
    files('spec-to-docbook.xsl'),
    '@INPUT@',
    '@OUTPUT@',
  ],
)

packagekit_transaction_ref_xml = custom_target(
  'org.freedesktop.PackageKit.Transaction.ref.xml',
  input: join_paths(meson.source_root(), 'src', 'org.freedesktop.PackageKit.Transaction.xml'),
  output: 'org.freedesktop.PackageKit.Transaction.ref.xml',
  build_by_default: true,
  command: [
    spec_to_docbook,
    files('spec-to-docbook.xsl'),
    '@INPUT@',
    '@OUTPUT@',
  ],
)
