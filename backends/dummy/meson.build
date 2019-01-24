shared_module(
	'pk_backend_dummy',
	'pk-backend-dummy.c',
	include_directories: packagekit_src_include,
	dependencies: [
		packagekit_glib2_dep,
		gmodule_dep,
	],
	c_args: [
		'-DPK_COMPILATION',
		'-DG_LOG_DOMAIN="PackageKit-Dummy"',
	],
	override_options: ['cpp_std=c++0x'],
	install: true,
	install_dir: pk_plugin_dir,
)
