shared_module(
	'pk_backend_yum',
	'pk-backend-yum.c',
	include_directories: packagekit_src_include,
	dependencies: [
		packagekit_glib2_dep,
		gmodule_dep,
	],
	c_args: [
		'-DPK_COMPILATION',
		'-DG_LOG_DOMAIN="PackageKit-Yum"',
		'-DSYSCONFDIR="@0@"'.format(get_option('sysconfdir')),
	],
	install: true,
	install_dir: pk_plugin_dir,
)

install_data(
	'Yum.conf',
	install_dir: join_paths(get_option('sysconfdir'), 'PackageKit'),
)

install_data(
	'yum-comps-groups.conf',
	'licenses.txt',
	install_dir: join_paths(get_option('datadir'), 'PackageKit', 'helpers', 'yum'),
)

install_data(
	'yumBackend.py',
	'yumComps.py',
	'yumFilter.py',
	install_dir: join_paths(get_option('datadir'), 'PackageKit', 'helpers', 'yum'),
	install_mode: 'rwxr--r--',
)
