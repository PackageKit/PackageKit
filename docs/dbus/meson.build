# Meson config for PackageKit D-Bus documentation

packagekit_dbus_md = custom_target(
    'dbus-docs',
    input: [
        join_paths(source_root, 'src', 'org.freedesktop.packagekit1.xml'),
        join_paths(source_root, 'src', 'org.freedesktop.packagekit1.Transaction.xml'),
    ],
    output: [
        'dbus-org.freedesktop.packagekit1.md',
        'dbus-org.freedesktop.packagekit1.Offline.md',
        'dbus-org.freedesktop.packagekit1.Transaction.md',
    ],
    command: [
        find_program('gdbus-codegen'),
        '--generate-md', 'dbus',
        '--output-directory', '@OUTDIR@',
        '@INPUT@',
    ],
)
