plugindir = @PK_PLUGIN_DIR@
plugin_LTLIBRARIES = libpk_backend_ipkg.la
libpk_backend_ipkg_la_SOURCES = pk-backend-ipkg.c
libpk_backend_ipkg_la_LIBADD = @PK_PLUGIN_LIBS@ $(IPKG_LIBS)

libpk_backend_ipkg_la_LDFLAGS = -module -avoid-version
libpk_backend_ipkg_la_CFLAGS = @PK_PLUGIN_CFLAGS@ $(IPKG_CFLAGS) 

