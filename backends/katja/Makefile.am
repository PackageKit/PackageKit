AM_CPPFLAGS = \
	-DG_LOG_DOMAIN=\"PackageKit-Katja\" \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DLOCALSTATEDIR=\""$(localstatedir)"\"

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_katja.la
libpk_backend_katja_la_SOURCES = pk-backend-katja.c \
									katja-pkgtools.c \
									katja-binary.c \
									katja-slackpkg.c \
									katja-dl.c
libpk_backend_katja_la_LIBADD = $(PK_PLUGIN_LIBS) ${KATJA_LIBS}
libpk_backend_katja_la_LDFLAGS = -module -avoid-version
libpk_backend_katja_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C) $(KATJA_CFLAGS)

confdir = $(sysconfdir)/PackageKit
conf_DATA = conf/Katja.conf

EXTRA_DIST = \
	$(conf_DATA)

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
