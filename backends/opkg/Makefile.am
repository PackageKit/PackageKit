INCLUDES = \
	-DG_LOG_DOMAIN=\"PackageKit-Opkg\"

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_opkg.la
libpk_backend_opkg_la_SOURCES = pk-backend-opkg.c
libpk_backend_opkg_la_LIBADD = $(PK_PLUGIN_LIBS) $(OPKG_LIBS)

libpk_backend_opkg_la_LDFLAGS = -module -avoid-version
libpk_backend_opkg_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(OPKG_CFLAGS) $(WARNINGFLAGS_C)


-include $(top_srcdir)/git.mk
