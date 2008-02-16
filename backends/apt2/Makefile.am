plugindir = @PK_PLUGIN_DIR@
plugin_LTLIBRARIES = libpk_backend_apt2.la
libpk_backend_apt2_la_SOURCES = pk-backend-apt2.c
libpk_backend_apt2_la_LIBADD = @PK_PLUGIN_LIBS@
libpk_backend_apt2_la_LDFLAGS = -module -avoid-version
libpk_backend_apt2_la_CFLAGS = @PK_PLUGIN_CFLAGS@

dbusinstancedir = $(LIBEXECDIR)
dbusinstance_DATA =                                     \
        aptDBUSBackend.py                               \
        $(NULL)

EXTRA_DIST =                                            \
        $(dbusinstance_DATA)                            \
        $(NULL)

install-data-hook:
	chmod a+rx $(DESTDIR)$(libexecdir)/*.py

clean-local :
	rm -f *~
	rm -f *.pyc

