pk_gettext_domain = 'PackageKit'

i18n.gettext(
  pk_gettext_domain,
  preset: 'glib',
  args: [
        '--default-domain=' + pk_gettext_domain,
        '--from-code=UTF-8',
        '--keyword=Description',
        '-i', '-F', '-c', '--no-wrap',
        '--package-name=' + pk_gettext_domain,
        '--copyright-holder=Matthias Klumpp',
        '--msgid-bugs-address=packagekit@lists.freedesktop.org'
    ]
)

run_target ('make-linguas',
    command: ['sh',
              join_paths(source_root, 'contrib/update-linguas.sh')]
)

# maintainer shortcut for updating l10n data
run_target ('l10n-update',
    command: ['ninja', '-C', meson.project_build_root(),
              'make-linguas',
              pk_gettext_domain + '-pot',
    ]
)
