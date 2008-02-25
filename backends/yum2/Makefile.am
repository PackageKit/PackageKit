SUBDIRS = helpers
plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_yum2.la
libpk_backend_yum2_la_SOURCES = pk-backend-yum2.c
libpk_backend_yum2_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_yum2_la_LDFLAGS = -module -avoid-version
libpk_backend_yum2_la_CFLAGS = $(PK_PLUGIN_CFLAGS)

clean-local:
	rm -f *.gcno

