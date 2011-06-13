INCLUDES = \
	-DG_LOG_DOMAIN=\"PackageKit-Pisi\"

helperdir = $(datadir)/PackageKit/helpers/pisi
dist_helper_DATA = 			\
	pisiBackend.py

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_pisi.la
libpk_backend_pisi_la_SOURCES = pk-backend-pisi.c
libpk_backend_pisi_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_pisi_la_LDFLAGS = -module -avoid-version
libpk_backend_pisi_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *~


-include $(top_srcdir)/git.mk
