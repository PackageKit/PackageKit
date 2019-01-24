if get_option('backend-name') == 'yum'
	subdir('yum-packagekit')
endif

configuration_msg += '''
GStreamer plugin:          @0@'''.format(get_option('gstreamer-plugin'))
if get_option('gstreamer-plugin')
	subdir('gstreamer-plugin')
endif

configuration_msg += '''
Pango module (GTK+3):      @0@'''.format(get_option('gtk-module'))
if get_option('gtk-module')
	subdir('gtk-module')
endif

configuration_msg += '''
BASH Command not found:    @0@'''.format(get_option('bash-command-not-found'))
if get_option('bash-command-not-found')
	subdir('command-not-found')
endif

configuration_msg += '''
BASH Completion:           @0@'''.format(get_option('bash-completion'))
if get_option('bash-completion')
	bash_completion_dep = dependency('bash-completion', version: '>=2.0')
	install_data(
		'pkcon',
		install_dir: bash_completion_dep.get_pkgconfig_variable('completionsdir')
	)
endif

configuration_msg += '''
Cron scripts:              @0@'''.format(get_option('cron'))
if get_option('cron')
	subdir('cron')
endif
