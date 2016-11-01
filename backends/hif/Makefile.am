plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_hif.la
EXTRA_DIST =								\
	hif-backend-vendor-fedora.c					\
	hif-backend-vendor-mageia.c
libpk_backend_hif_la_SOURCES =						\
	hif-backend-vendor.h						\
	hif-backend.c							\
	hif-backend.h							\
	pk-backend-hif.c
nodist_libpk_backend_hif_la_SOURCES = hif-backend-vendor-@HIF_VENDOR@.c
libpk_backend_hif_la_LIBADD = $(PK_PLUGIN_LIBS) $(HIF_LIBS)
libpk_backend_hif_la_CPPFLAGS =						\
	$(HIF_CFLAGS)							\
	-DG_LOG_DOMAIN=\"PackageKit-Hif\"
libpk_backend_hif_la_LDFLAGS = -module -avoid-version
libpk_backend_hif_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

-include $(top_srcdir)/git.mk
