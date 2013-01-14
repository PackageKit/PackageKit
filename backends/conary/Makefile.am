AM_CPPFLAGS = \
	-DG_LOG_DOMAIN=\"PackageKit-Conary\"

helperdir = $(datadir)/PackageKit/helpers/conary
dist_helper_DATA = 			\
	conaryBackend.py		\
	conaryFilter.py			\
	conaryCallback.py		\
	conaryInit.py			\
	XMLCache.py			\
	generateXML.py			\
	pkConaryLog.py			\
	conarypk.py			\
	conaryProgress.py		\
	conaryEnums.py

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_conary.la
libpk_backend_conary_la_SOURCES = pk-backend-conary.c
libpk_backend_conary_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_conary_la_LDFLAGS = -module -avoid-version
libpk_backend_conary_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
