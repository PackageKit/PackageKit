## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

EXTRA_DIST =						\
	pk-debuginfo-install.xml			\
	$(NULL)

if HAVE_XSLTPROC
man_MANS =						\
	pk-debuginfo-install.1				\
	$(NULL)
endif

if HAVE_XSLTPROC
pk-debuginfo-install.1: pk-debuginfo-install.xml
	xsltproc http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $? &> /dev/null
endif

AM_CPPFLAGS =						\
	$(PIE_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	-DPK_COMPILATION				\
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"		\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-I$(top_builddir)/lib				\
	-I$(top_srcdir)/lib

bin_PROGRAMS = pk-debuginfo-install

PK_GLIB2_LIBS =						\
	$(top_builddir)/lib/packagekit-glib2/libpackagekit-glib2.la	\
	$(top_builddir)/lib/packagekit-glib2/libpackagekitprivate.a	\
	$(NULL)

pk_debuginfo_install_SOURCES =				\
	pk-debuginfo-install.c				\
	$(NULL)

pk_debuginfo_install_LDADD =				\
	$(GLIB_LIBS)					\
	$(PK_GLIB2_LIBS)				\
	$(NULL)

pk_debuginfo_install_LDFLAGS =				\
	$(PIE_LDFLAGS)

pk_debuginfo_install_CFLAGS =				\
	$(WARNINGFLAGS_C)				\
	$(NULL)

CLEANFILES = $(man_MANS)

clean-local:
	rm -f *~
	rm -f *.1
	rm -f manpage.*
	rm -f *.out


-include $(top_srcdir)/git.mk
