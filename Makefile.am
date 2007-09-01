SUBDIRS = 			\
	policy			\
	man			\
	data			\
	libselftest		\
	libgbus			\
	libpackagekit		\
	src			\
	client			\
	helpers			\
	$(NULL)

AUTOMAKE_OPTIONS = 1.7

NULL =

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = packagekit.pc

dbusdir = ${SYSCONFDIR}/dbus-1/system.d
dist_dbus_DATA = PackageKit.conf

EXTRA_DIST =			\
	PackageKit.conf.in	\
	packagekit.pc.in	\
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
	packagekit.pc		\
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
