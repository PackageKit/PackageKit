SUBDIRS = helpers
plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_smart.la
libpk_backend_smart_la_SOURCES = pk-backend-smart.c
libpk_backend_smart_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_smart_la_LDFLAGS = -module -avoid-version
libpk_backend_smart_la_CFLAGS = $(PK_PLUGIN_CFLAGS)
