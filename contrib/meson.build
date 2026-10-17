if get_option('gstreamer-plugin')
    subdir('gstreamer-plugin')
endif

if get_option('gtk-module')
    subdir('gtk-module')
endif

if get_option('bash-command-not-found')
    subdir('command-not-found')
endif

if get_option('bash-completion')
    bash_completion_dep = dependency('bash-completion', version: '>=2.0')
    install_data(
        'pkgcli',
        install_dir: bash_completion_dep.get_variable(
            pkgconfig: 'completionsdir',
            pkgconfig_define: ['prefix', get_option('prefix')])
    )
endif

if get_option('cron')
    subdir('cron')
endif
