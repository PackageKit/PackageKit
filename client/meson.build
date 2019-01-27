executable(
	'pkcon',
	'pk-console.c',
	dependencies: packagekit_glib2_dep,
	install: true,
	install_dir: get_option('libexecdir'),
	c_args: [
		'-DPK_COMPILATION=1',
		'-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE=1',
		'-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
		'-DPACKAGE_LOCALE_DIR="@0@"'.format(package_locale_dir),
		'-DVERSION="@0@"'.format(meson.project_version()),
	]
)

executable(
	'pkmon',
	'pk-monitor.c',
	dependencies: packagekit_glib2_dep,
	install: true,
	c_args: [
		'-DPK_COMPILATION=1',
		'-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE=1',
		'-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
		'-DPACKAGE_LOCALE_DIR="@0@"'.format(package_locale_dir),
		'-DVERSION="@0@"'.format(meson.project_version()),
	]
)

if get_option('offline_update')
	executable(
		'pk-offline-update',
		'pk-offline-update.c',
		dependencies: [
			packagekit_glib2_dep,
			libsystemd,
		],
		install: true,
		install_dir: get_option('libexecdir'),
		c_args: [
			'-DPK_COMPILATION=1',
			'-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE=1',
			'-DGETTEXT_PACKAGE="@0@"'.format(meson.project_name()),
			'-DPACKAGE_LOCALE_DIR="@0@"'.format(package_locale_dir),
		]
	)
endif

if get_option('man_pages')
	xsltproc = find_program('xsltproc')
	pkcon_man = custom_target(
		'pkcon.1',
		input: 'pkcon.xml',
		output: 'pkcon.1',
		command: [
			xsltproc,
			'--output', '@OUTPUT@',
			'http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl',
			'@INPUT@',
		],
		install: true,
		install_dir: join_paths(get_option('datadir'), 'man'),
	)
	pkmon_man = custom_target(
		'pkmon.1',
		input: 'pkmon.xml',
		output: 'pkmon.1',
		command: [
			xsltproc,
			'--output', '@OUTPUT@',
			'http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl',
			'@INPUT@',
		],
		install: true,
		install_dir: join_paths(get_option('datadir'), 'man'),
	)
endif
