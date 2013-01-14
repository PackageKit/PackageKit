AM_CPPFLAGS = \
	-DG_LOG_DOMAIN=\"PackageKit-Box\"

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_box.la
libpk_backend_box_la_SOURCES = pk-backend-box.c
libpk_backend_box_la_LIBADD = $(PK_PLUGIN_LIBS) $(BOX_LIBS)
libpk_backend_box_la_LDFLAGS = -module -avoid-version
libpk_backend_box_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)
libpk_backend_box_la_AM_CPPFLAGS = $(BOX_CFLAGS)


-include $(top_srcdir)/git.mk
