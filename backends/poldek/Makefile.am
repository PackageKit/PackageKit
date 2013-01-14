AM_CPPFLAGS = \
	-DG_LOG_DOMAIN=\"PackageKit-Poldek\"

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_poldek.la
libpk_backend_poldek_la_SOURCES = pk-backend-poldek.c
libpk_backend_poldek_la_LIBADD = $(PK_PLUGIN_LIBS) $(POLDEK_LIBS)
libpk_backend_poldek_la_LDFLAGS = -module -avoid-version
libpk_backend_poldek_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(POLDEK_CFLAGS) $(WARNINGFLAGS_C)

-include $(top_srcdir)/git.mk
