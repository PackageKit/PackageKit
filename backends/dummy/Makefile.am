AM_CPPFLAGS = \
	-DG_LOG_DOMAIN=\"PackageKit-Dummy\"

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_dummy.la
libpk_backend_dummy_la_SOURCES = pk-backend-dummy.c
libpk_backend_dummy_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_dummy_la_LDFLAGS = -module -avoid-version
libpk_backend_dummy_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)


-include $(top_srcdir)/git.mk
