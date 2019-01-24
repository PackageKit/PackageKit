appstream_dep = dependency('appstream-glib')
dnf_dep = dependency('libdnf', version: '>=0.22.0')
rpm_dep = dependency('rpm')

configuration_msg += '''
DNF Vendor:                @0@'''.format(get_option('dnf_vendor'))

shared_module(
	'pk_backend_dnf',
	'dnf-backend-vendor-@0@.c'.format(get_option('dnf_vendor')),
	'dnf-backend-vendor.h',
	'dnf-backend.c',
	'dnf-backend.h',
	'pk-backend-dnf.c',
	include_directories: packagekit_src_include,
	dependencies: [
		packagekit_glib2_dep,
		appstream_dep,
		dnf_dep,
		rpm_dep,
		gmodule_dep,
	],
	c_args: [
		'-DPK_COMPILATION',
		'-DG_LOG_DOMAIN="PackageKit-DNF"',
	],
	install: true,
	install_dir: pk_plugin_dir,
)
