plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_apt.la
libpk_backend_apt_la_SOURCES = pk-backend-apt.c
libpk_backend_apt_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_apt_la_LDFLAGS = -module -avoid-version
libpk_backend_apt_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

aptconfdir = ${SYSCONFDIR}/apt/apt.conf.d
aptconf_DATA = 20packagekit

helperdir = $(datadir)/PackageKit/helpers/apt
dist_helper_DATA =					\
	aptbackend.py					\
	$(NULL)

sbin_SCRIPTS =						\
	update-packagekit-app-data			\
	$(NULL)

EXTRA_DIST =						\
	20packagekit					\
	update-packagekit-app-data			\
	$(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(libexecdir)/*.py

clean-local :
	rm -f *~
	rm -f *.pyc
