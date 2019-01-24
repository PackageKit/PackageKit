add_languages('cpp')
cpp_compiler = meson.get_compiler('cpp')

gstreamer_dep = dependency('gstreamer-1.0')
gstreamer_base_dep = dependency('gstreamer-base-1.0')
gstreamer_plugins_base_dep = dependency('gstreamer-plugins-base-1.0')
appstream_dep = dependency('appstream', version: '>=0.11')

apt_pkg_dep = cpp_compiler.find_library('apt-pkg')
apt_inst_dep = cpp_compiler.find_library('apt-inst')

if not cpp_compiler.compiles(
	'''
		#include <apt-pkg/pkgsystem.h>
		int main () {
			_system->ArchitecturesSupported();
			return 0;
		}
	''',
	dependencies: [
		apt_pkg_dep,
		apt_inst_dep
	]
)
	error ('libapt-pkg 1.1 or later is required')
endif

# Check whether apt supports ddtp
ddtp_flag = []
if cpp_compiler.compiles(
	'''
		#include <apt-pkg/pkgcache.h>
		int main () {
			pkgCache::DescIterator d;
			return 0;
		}
	''',
	dependencies: [
		apt_pkg_dep,
		apt_inst_dep
	]
)
	ddtp_flag = ['-DHAVE_DDTP']
endif

shared_module(
	'pk_backend_aptcc',
	'acqpkitstatus.cpp',
	'acqpkitstatus.h',
	'gst-matcher.cpp',
	'gst-matcher.h',
	'apt-messages.cpp',
	'apt-messages.h',
	'apt-utils.cpp',
	'apt-utils.h',
	'apt-sourceslist.cpp',
	'apt-sourceslist.h',
	'apt-cache-file.cpp',
	'apt-cache-file.h',
	'apt-intf.cpp',
	'apt-intf.h',
	'pkg-list.cpp',
	'pkg-list.h',
	'deb-file.cpp',
	'deb-file.h',
	'pk-backend-aptcc.cpp',
	include_directories: packagekit_src_include,
	dependencies: [
		packagekit_glib2_dep,
		gmodule_dep,
		apt_pkg_dep,
		apt_inst_dep,
		gstreamer_dep,
		gstreamer_base_dep,
		gstreamer_plugins_base_dep,
		appstream_dep,
	],
	cpp_args: [
		'-DPK_COMPILATION',
		'-DG_LOG_DOMAIN="PackageKit-APTcc"',
		'-DDATADIR="@0@"'.format(join_paths(get_option('prefix'), get_option('datadir'))),
		ddtp_flag,
	],
	override_options: ['c_std=c11', 'cpp_std=c++11'],
	install: true,
	install_dir: pk_plugin_dir,
)

install_data(
	'20packagekit',
	install_dir: join_paths(get_option('sysconfdir'), 'apt', 'apt.conf.d'),
)

install_data(
	'pkconffile.nodiff',
	install_dir: join_paths(get_option('datadir'), 'PackageKit', 'helpers', 'aptcc'),
)
