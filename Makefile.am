AUTOMAKE_OPTIONS = 1.7

NULL =

SUBDIRS = 						\
	docs						\
	html						\
	policy						\
	etc						\
	man						\
	data						\
	libselftest					\
	libgbus						\
	libpackagekit					\
	src						\
	client						\
	backends					\
	$(NULL)

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = packagekit.pc

dbusdir = ${SYSCONFDIR}/dbus-1/system.d
dist_dbus_DATA = org.freedesktop.PackageKit.conf

packagekitpythondir = ${PYTHON_PACKAGE_DIR}
packagekitpython_PYTHON =				\
	python/packagekit/__init__.py			\
	python/packagekit/backend.py			\
	python/packagekit/frontend.py			\
	$(NULL)
	

EXTRA_DIST =						\
	org.freedesktop.PackageKit.conf.in		\
	packagekit.pc.in				\
	COPYING						\
	MAINTAINERS					\
	AUTHORS						\
	INSTALL						\
	README						\
	HACKING						\
	NEWS						\
	ChangeLog					\
	$(NULL)

clean-local :
	rm -f *~

security-check:
	flawfinder `find -name "*.c"`

DISTCLEANFILES =					\
	PackageKit-*.tar.gz				\
	packagekit.pc					\
	intltool-extract				\
	intltool-merge					\
	intltool-update					\
	$(NULL)

MAINTAINERCLEANFILES =					\
	*~						\
	intltool-*.in					\
	compile						\
	configure					\
	INSTALL						\
	install-sh					\
	missing						\
	mkinstalldirs					\
	config.guess					\
	ltmain.sh					\
	config.sub					\
	depcomp						\
	Makefile.in					\
	config.h.*					\
	aclocal.m4					\
	$(NULL)

