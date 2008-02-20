SUBDIRS = helpers
plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_conary.la
libpk_backend_conary_la_SOURCES = pk-backend-conary.c
libpk_backend_conary_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_conary_la_LDFLAGS = -module -avoid-version
libpk_backend_conary_la_CFLAGS = $(PK_PLUGIN_CFLAGS)

