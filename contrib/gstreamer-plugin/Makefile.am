## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

AM_CPPFLAGS =						\
	$(PIE_CFLAGS)					\
	$(PK_GSTREAMER_PLUGIN_CFLAGS)			\
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
	-DBINDIR=\"$(bindir)\"			 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DVERSION="\"$(VERSION)\"" 			\
	-DPK_DATA=\"$(pkgdatadir)\"			\
	-DPK_DB_DIR=\""$(PK_DB_DIR)"\" 			\
	-I$(top_builddir)				\
	-I$(top_builddir)/lib				\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/lib

PK_GLIB2_LIBS =						\
	$(top_builddir)/lib/packagekit-glib2/libpackagekit-glib2.la

libexec_PROGRAMS =					\
	pk-gstreamer-install

pk_gstreamer_install_SOURCES =				\
	pk-gstreamer-install.c

pk_gstreamer_install_LDADD =				\
	$(PK_GSTREAMER_PLUGIN_LIBS)			\
	$(PK_GLIB2_LIBS)

pk_gstreamer_install_LDFLAGS =				\
	$(PIE_LDFLAGS)

pk_gstreamer_install_CFLAGS =				\
	$(WARNINGFLAGS_C)

clean-local:
	rm -f *~

EXTRA_DIST =						\
	README

MAINTAINERCLEANFILES =					\
	*~						\
	Makefile.in


-include $(top_srcdir)/git.mk
