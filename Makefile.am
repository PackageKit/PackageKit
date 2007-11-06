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
	python						\
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
	python/packagekit/enums.py				\
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
	rm -f *~ *.bb *.bbg *.da *.gcov .libs/*.da .libs/*.bbg

security-check:
	flawfinder `find -name "*.c"`

if PK_BUILD_GCOV
gcov:
	make -C src gcov
	make -C libpackagekit gcov
	cat src/gcov.txt > gcov.txt
	cat libpackagekit/gcov.txt >> gcov.txt
	cat gcov.txt
endif

if PK_BUILD_GPROF
gprof:
	make -C src gprof
	make -C libpackagekit gprof
	cat src/gprof.txt > gprof.txt
	cat libpackagekit/gprof.txt >> gprof.txt
	cat gprof.txt
endif

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

DISTCHECK_CONFIGURE_FLAGS = --disable-tests --disable-gcov

