INCLUDES = \
	-DG_LOG_DOMAIN=\"PackageKit-Razor\"

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_razor.la
libpk_backend_razor_la_SOURCES = pk-backend-razor.c
libpk_backend_razor_la_LIBADD = $(PK_PLUGIN_LIBS) $(RAZOR_LIBS)

libpk_backend_razor_la_LDFLAGS = -module -avoid-version
libpk_backend_razor_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(RAZOR_CFLAGS) $(WARNINGFLAGS_C)


-include $(top_srcdir)/git.mk
