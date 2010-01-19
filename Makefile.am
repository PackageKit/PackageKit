AUTOMAKE_OPTIONS = 1.7

NULL =

SUBDIRS = 						\
	po						\
	policy						\
	etc						\
	data						\
	lib						\
	src						\
	client						\
	tools						\
	backends					\
	contrib						\
	docs						\
	$(NULL)

clean-local :
	rm -f *~

security-check:
	flawfinder `find -name "*.c"`

snapshot:
	$(MAKE) dist distdir=$(PACKAGE)-$(VERSION)-`date +"%Y%m%d"`

DISTCLEANFILES =					\
	PackageKit-*.tar.gz				\
	packagekit.pc					\
	intltool-extract				\
	intltool-merge					\
	intltool-update					\
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
	moc.mk						\
        intltool-extract.in				\
        intltool-merge.in				\
        intltool-update.in				\
	$(NULL)

DISTCHECK_CONFIGURE_FLAGS = 				\
	--enable-gtk-doc				\
	--with-security-framework=dummy			\
	--disable-tests					\
	$(NULL)

distclean-local:
	if test $(srdcir) = .; then :; else \
		rm -f ChangeLog; \
	fi

ChangeLog:
	@echo Creating $@
	@if test -d "$(srcdir)/.git"; then \
	  (GIT_DIR=$(top_srcdir)/.git ./missing --run git log PACKAGEKIT_0_5_X.. --stat -M -C --name-status  --date=short --no-color) | fmt --split-only > $@.tmp \
	  && mv -f $@.tmp $@ \
	  || ($(RM) $@.tmp; \
	      echo Failed to generate ChangeLog, your ChangeLog may be outdated >&2; \
	      (test -f $@ || echo git-log is required to generate this file >> $@)); \
	else \
	  test -f $@ || \
	  (echo A git checkout and git-log is required to generate ChangeLog >&2 && \
	  echo A git checkout and git-log is required to generate this file >> $@); \
	fi

.PHONY: ChangeLog
