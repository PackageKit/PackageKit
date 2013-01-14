AM_CPPFLAGS = \
	-DG_LOG_DOMAIN=\"PackageKit-Entropy\"

helperdir = $(datadir)/PackageKit/helpers/entropy
dist_helper_DATA = entropyBackend.py

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_entropy.la
libpk_backend_entropy_la_SOURCES = pk-backend-entropy.c
libpk_backend_entropy_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_entropy_la_LDFLAGS = -module -avoid-version
libpk_backend_entropy_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *~
	rm -f *.pyc

-include $(top_srcdir)/git.mk
