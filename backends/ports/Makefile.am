INCLUDES = \
	-DG_LOG_DOMAIN=\"PackageKit-Ports\"

SUBDIRS=ruby_packagekit

helperdir = $(datadir)/PackageKit/helpers/ports
dist_helper_DATA = portsBackend.rb

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_ports.la
libpk_backend_ports_la_SOURCES = pk-backend-ports.c
libpk_backend_ports_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_ports_la_LDFLAGS = -module -avoid-version
libpk_backend_ports_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.rb

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
