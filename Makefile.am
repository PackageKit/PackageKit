SUBDIRS = src client

AUTOMAKE_OPTIONS = 1.7

NULL =

dbusdir = ${SYSCONFDIR}/dbus-1/system.d
dist_dbus_DATA = PackageKit.conf

clean-local :
	rm -f *~

EXTRA_DIST =			\
	PackageKit.conf.in				\
	COPYING			\
	MAINTAINERS		\
	AUTHORS			\
	INSTALL			\
	README			\
	HACKING			\
	NEWS			\
	ChangeLog		\
	$(NULL)

DISTCLEANFILES =					\
	PackageKit-*.tar.gz		\
	$(NULL)

MAINTAINERCLEANFILES =		\
	*~			\
	intltool-*.in		\
	compile			\
	configure		\
	INSTALL			\
	install-sh		\
	missing			\
	mkinstalldirs		\
	config.guess		\
	ltmain.sh		\
	config.sub		\
	depcomp			\
	Makefile.in		\
	config.h.*		\
	aclocal.m4		\
	$(NULL)
