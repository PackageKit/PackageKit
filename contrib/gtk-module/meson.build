gtk3_dep = dependency('gtk+-3.0', version: '>=3.0.0')
pangoft2_dep = dependency('pangoft2')
fontconfig_dep = dependency('fontconfig')

shared_module(
	'pk-gtk-module',
	'pk-gtk-module.c',
	dependencies: [
		packagekit_glib2_dep,
		gtk3_dep,
		pangoft2_dep,
		fontconfig_dep,
	],
	install: true,
	install_dir: join_paths(get_option('libdir'), 'gtk-3.0', 'modules'),
	c_args: [
		'-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE=1',
		'-DVERSION="@0@"'.format(meson.project_version()),
	]
)

install_data(
	'pk-gtk-module.desktop',
	install_dir: join_paths(get_option('libdir'), 'gnome-settings-daemon-3.0', 'gtk-modules')
)

subdir('gtk2')
