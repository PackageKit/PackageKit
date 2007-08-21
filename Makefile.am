SUBDIRS = policy libgbus src client helpers

AUTOMAKE_OPTIONS = 1.7

NULL =

dbusdir = ${SYSCONFDIR}/dbus-1/system.d
dist_dbus_DATA = PackageKit.conf

EXTRA_DIST =			\
	PackageKit.conf.in	\
	COPYING			\
	MAINTAINERS		\
	AUTHORS			\
	INSTALL			\
	README			\
	HACKING			\
	NEWS			\
	ChangeLog		\
	$(NULL)

clean-local :
	rm -f *~

DISTCLEANFILES =		\
	PackageKit-*.tar.gz	\
	intltool-extract	\
	intltool-merge		\
	intltool-update		\
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
