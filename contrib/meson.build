if get_option('gstreamer_plugin')
  subdir('gstreamer-plugin')
endif

if get_option('gtk_module')
  subdir('gtk-module')
endif

if get_option('bash_command_not_found')
  subdir('command-not-found')
endif

if get_option('bash_completion')
  bash_completion_dep = dependency('bash-completion', version: '>=2.0')
  install_data(
    'pkgctl',
    install_dir: bash_completion_dep.get_variable(
                    pkgconfig: 'completionsdir',
                    pkgconfig_define: ['prefix', get_option('prefix')])
  )

  if get_option('legacy_tools')
    install_data(
        'pkcon',
        install_dir: bash_completion_dep.get_variable(
                        pkgconfig: 'completionsdir',
                        pkgconfig_define: ['prefix', get_option('prefix')])
    )
  endif
endif

if get_option('cron')
  subdir('cron')
endif
