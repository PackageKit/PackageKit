AM_CPPFLAGS = \
	-DG_LOG_DOMAIN=\"PackageKit-Slackware\" \
	-DSYSCONFDIR=\""$(sysconfdir)"\" \
	-DLOCALSTATEDIR=\""$(localstatedir)"\"

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_slack.la
libpk_backend_slack_la_SOURCES = \
	pk-backend-slack.c \
	slack-utils.c \
	slack-pkgtools.c \
	slack-slackpkg.c \
	slack-dl.c
libpk_backend_slack_la_LIBADD = -lbz2 $(PK_PLUGIN_LIBS) ${SLACK_LIBS}
libpk_backend_slack_la_LDFLAGS = -module -avoid-version
libpk_backend_slack_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(AM_CPPFLAGS) $(WARNINGFLAGS_C)
libpk_backend_slack_la_CPPFLAGS = $(PK_PLUGIN_CFLAGS) $(AM_CPPFLAGS) $(SLACK_CFLAGS) $(WARNINGFLAGS_CPP)

confdir = $(sysconfdir)/PackageKit
conf_in_files = Slackware.conf.in
conf_DATA = $(conf_in_files:.conf.in=.conf)

$(conf_DATA): $(conf_in_files) Makefile
	@sed -e "s|\@pkgmain\@|$(SLACK_PKGMAIN)|" $< > $@

databasedir = $(localstatedir)/cache/PackageKit/metadata
database_DATA =	metadata.db

EXTRA_DIST = \
	$(conf_in_files) \
	$(database_DATA)

clean-local:
	rm -f *~

-include $(top_srcdir)/git.mk
