AUTOMAKE_OPTIONS = 1.7

ACLOCAL_AMFLAGS = -I m4 ${ACLOCAL_FLAGS}

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
	docs

clean-local :
	rm -f *~

security-check:
	flawfinder `find -name "*.c"`

snapshot:
	$(MAKE) dist distdir=$(PACKAGE)-$(VERSION)-`date +"%Y%m%d"`

DISTCLEANFILES =					\
	PackageKit-*.tar.*

MAINTAINERCLEANFILES =					\
	$(srcdir)/INSTALL				\
	$(srcdir)/ABOUT-NLS				\
	$(srcdir)/aclocal.m4				\
	$(srcdir)/autoscan.log				\
	$(srcdir)/build-aux				\
	$(srcdir)/config.rpath				\
	$(srcdir)/config.h.in				\
	$(srcdir)/configure.scan			\
	$(srcdir)/mkinstalldirs				\
	$(srcdir)/omf.make				\
	$(srcdir)/xmldocs.make				\
	$(srcdir)/gtk-doc.make				\
	$(srcdir)/ChangeLog				\
	`find "$(srcdir)" -type f -name Makefile -print` \
	`find "$(srcdir)" -type f -name Makefile.in -print` \
	`find "$(srcdir)" -type f -name *.pyc -print`

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
	--with-systemdsystemunitdir=/tmp		\
	--disable-tests

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

-include $(top_srcdir)/git.mk
