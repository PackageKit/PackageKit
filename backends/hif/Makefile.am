plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_hif.la
libpk_backend_hif_la_SOURCES =						\
	hif-backend.c							\
	hif-backend.h							\
	pk-backend-hif.c
libpk_backend_hif_la_LIBADD = $(PK_PLUGIN_LIBS) $(HIF_LIBS)
libpk_backend_hif_la_CPPFLAGS =						\
	$(HIF_CFLAGS)							\
	-DG_LOG_DOMAIN=\"PackageKit-Hif\"
libpk_backend_hif_la_LDFLAGS = -module -avoid-version
libpk_backend_hif_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

-include $(top_srcdir)/git.mk
