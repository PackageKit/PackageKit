subdir('ruby_packagekit')

shared_module(
	'pk_backend_ports',
	'pk-backend-ports.c',
	include_directories: packagekit_src_include,
	dependencies: [
		packagekit_glib2_dep,
		gmodule_dep,
	],
	c_args: [
		'-DPK_COMPILATION',
		'-DG_LOG_DOMAIN="PackageKit-Ports"',
	],
	install: true,
	install_dir: pk_plugin_dir,
)

install_data(
	'portsBackend.rb',
	install_dir: join_paths(get_option('datadir'), 'PackageKit', 'helpers', 'ports'),
	install_mode: 'rwxr--r--',
)
