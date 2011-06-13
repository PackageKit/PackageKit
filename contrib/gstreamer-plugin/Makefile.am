## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

INCLUDES =						\
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
	-I$(top_srcdir)/lib				\
	$(NULL)

PK_GLIB2_LIBS =						\
	$(top_builddir)/lib/packagekit-glib2/libpackagekit-glib2.la	\
	$(NULL)

libexec_PROGRAMS =					\
	pk-gstreamer-install				\
	$(NULL)

pk_gstreamer_install_SOURCES =				\
	pk-gstreamer-install.c				\
	$(NULL)

pk_gstreamer_install_LDADD =				\
	$(PK_GSTREAMER_PLUGIN_LIBS)			\
	$(PK_GLIB2_LIBS)				\
	$(NULL)

pk_gstreamer_install_CFLAGS =				\
	$(WARNINGFLAGS_C)				\
	$(NULL)

clean-local:
	rm -f *~

EXTRA_DIST =						\
	README						\
	$(NULL)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)


-include $(top_srcdir)/git.mk
