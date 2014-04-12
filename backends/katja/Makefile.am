AM_CPPFLAGS = \
	-DG_LOG_DOMAIN=\"PackageKit-Katja\" \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DLOCALSTATEDIR=\""$(localstatedir)"\"

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_katja.la
libpk_backend_katja_la_SOURCES = \
	pk-backend-katja.c \
	katja-pkgtools.c \
	katja-pkgtools.h \
	katja-binary.c \
	katja-binary.h \
	katja-slackpkg.c \
	katja-slackpkg.h \
	katja-dl.c \
	katja-dl.h \
	katja-utils.c \
	katja-utils.h
libpk_backend_katja_la_LIBADD = -lbz2 $(PK_PLUGIN_LIBS) ${KATJA_LIBS}
libpk_backend_katja_la_LDFLAGS = -module -avoid-version
libpk_backend_katja_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C) $(KATJA_CFLAGS)

confdir = $(sysconfdir)/PackageKit
conf_in_files = Katja.conf.in
conf_DATA = $(conf_in_files:.conf.in=.conf)

$(conf_DATA): $(conf_in_files) Makefile
	@sed -e "s|\@pkgmain\@|$(KATJA_PKGMAIN)|" $< > $@


databasedir = $(localstatedir)/cache/PackageKit/metadata
database_DATA =	metadata.db

EXTRA_DIST = \
	$(conf_in_files) \
	$(database_DATA)

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
