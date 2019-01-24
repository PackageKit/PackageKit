gstreamer_dep = dependency('gstreamer-1.0')
gstreamer_plugins_base_dep = dependency('gstreamer-plugins-base-1.0')

executable(
	'pk-gstreamer-install',
	'pk-gstreamer-install.c',
	dependencies: [
		packagekit_glib2_dep,
		gstreamer_dep,
		gstreamer_plugins_base_dep,
	],
	install: true,
	install_dir: get_option('libexecdir'),
	c_args: [
		'-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE=1',
		'-DVERSION="@0@"'.format(meson.project_version()),
	]
)
