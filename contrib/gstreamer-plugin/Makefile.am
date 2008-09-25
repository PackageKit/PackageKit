## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

INCLUDES =						\
	$(PK_GSTREAMER_PLUGIN_CFLAGS)			\
	-DBINDIR=\"$(bindir)\"			 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DVERSION="\"$(VERSION)\"" 			\
	-DPK_DATA=\"$(pkgdatadir)\"			\
	-DPK_DB_DIR=\""$(PK_DB_DIR)"\" 			\
	-DEGG_LOG_FILE=\""$(PK_LOG_DIR)/PackageKit"\"	\
	$(NULL)

libexec_PROGRAMS =					\
	pk-gstreamer-install				\
	$(NULL)

pk_gstreamer_install_SOURCES =				\
	pk-gstreamer-install.c				\
	$(NULL)

pk_gstreamer_install_LDADD =				\
	$(PK_GSTREAMER_PLUGIN_LIBS)			\
	$(NULL)

clean-local:
	rm -f *~

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

