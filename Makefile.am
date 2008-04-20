AUTOMAKE_OPTIONS = 1.7

NULL =

SUBDIRS = 						\
	po						\
	policy						\
	etc						\
	man						\
	contrib						\
	data						\
	libselftest					\
	libgbus						\
	libpackagekit					\
	src						\
	client						\
	backends					\
	python						\
	docs						\
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

snapshot:
	$(MAKE) dist distdir=$(PACKAGE)-$(VERSION)-`date +"%Y%m%d"`

DISTCLEANFILES =					\
	PackageKit-*.tar.gz				\
	packagekit.pc					\
	intltool-extract				\
	intltool-merge					\
	intltool-update					\
	gcov.txt					\
	gprof.txt					\
	packagekit-decl-list.txt			\
	packagekit-decl.txt				\
	packagekit-overrides.txt			\
	packagekit-sections.txt				\
	packagekit.types				\
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

EXTRA_DIST =						\
	COPYING						\
	MAINTAINERS					\
	AUTHORS						\
	INSTALL						\
	README						\
	HACKING						\
        TODO						\
	NEWS						\
        autogen.sh					\
	config.h					\
	ChangeLog					\
        intltool-extract.in				\
        intltool-merge.in				\
        intltool-update.in				\
	$(NULL)

DISTCHECK_CONFIGURE_FLAGS = 				\
	--disable-gcov					\
	--enable-gtk-doc				\
	--with-security-framework=dummy			\
	--disable-developer				\
	$(NULL)

