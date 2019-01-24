gtk2_dep = dependency('gtk+-2.0', required: false)

configuration_msg += '''
Pango module (GTK+2):      @0@'''.format(gtk2_dep.found())

if gtk2_dep.found()
	shared_module(
		'pk-gtk2-module',
		'pk-gtk-module-gtk2.c',
		dependencies: [
			packagekit_glib2_dep,
			gtk2_dep,
		],
		install: true,
		install_dir: join_paths(get_option('libdir'), 'gtk-2.0', 'modules'),
		c_args: [
			'-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE=1',
			'-DVERSION="@0@"'.format(meson.project_version()),
		]
	)
endif
