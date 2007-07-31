SUBDIRS = src

EXTRA_DIST =						\
	PackageKit.conf.in				\
	HACKING						\
	MAINTAINERS

dbusdir = ${SYSCONFDIR}/dbus-1/system.d
dist_dbus_DATA = PackageKit.conf

clean-local :
	rm -f *~

DISTCLEANFILES =					\
	PackageKit-*.tar.gz

