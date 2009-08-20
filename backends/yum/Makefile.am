helperdir = $(datadir)/PackageKit/helpers/yum
dist_helper_DATA = 			\
	yum-comps-groups.conf		\
	yumBackend.py			\
	yumComps.py			\
	yumMediaManager.py		\
	yumFilter.py

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_yum.la
libpk_backend_yum_la_SOURCES = pk-backend-yum.c
libpk_backend_yum_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_yum_la_LDFLAGS = -module -avoid-version
libpk_backend_yum_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local:
	rm -f *.gcno
	rm -f *~

