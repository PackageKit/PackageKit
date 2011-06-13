INCLUDES = \
	-DG_LOG_DOMAIN=\"PackageKit-Smart\"

helperdir = $(datadir)/PackageKit/helpers/smart
dist_helper_DATA = 			\
	smartBackend.py

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_smart.la
libpk_backend_smart_la_SOURCES = pk-backend-smart.c
libpk_backend_smart_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_smart_la_LDFLAGS = -module -avoid-version
libpk_backend_smart_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py


-include $(top_srcdir)/git.mk
