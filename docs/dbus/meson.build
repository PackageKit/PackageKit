# Meson config for PackageKit D-Bus documentation

packagekit_dbus_md = custom_target(
    'dbus-docs',
    input: [
        join_paths(source_root, 'src', 'org.freedesktop.PackageKit.xml'),
        join_paths(source_root, 'src', 'org.freedesktop.PackageKit.Transaction.xml'),
    ],
    output: [
        'dbus-org.freedesktop.PackageKit.md',
        'dbus-org.freedesktop.PackageKit.Offline.md',
        'dbus-org.freedesktop.PackageKit.Transaction.md',
    ],
    command: [
        find_program('gdbus-codegen'),
        '--generate-md', 'dbus',
        '--output-directory', '@OUTDIR@',
        '@INPUT@',
    ],
)
