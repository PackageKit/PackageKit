AM_CPPFLAGS = \
	$(PK_PLUGIN_CFLAGS) \
	$(filter-out -std=c11,$(SLACK_CFLAGS)) \
	$(WARNINGFLAGS_CPP) \
	-DG_LOG_DOMAIN=\"PackageKit-Slackware\" \
	-DLOCALSTATEDIR=\""$(localstatedir)"\" \
	-I../

PK_BACKEND_SLACK_LIBS =	\
	-lbz2 -lcurl \
	$(SQLITE_LIBS) \
	$(GLIB_LIBS) \
	$(top_builddir)/lib/packagekit-glib2/libpackagekit-glib2.la \
	$(top_builddir)/backends/slack/libpk-backend-slack.a

check_PROGRAMS = \
	slack-slackpkg-test \
	slack-dl-test \
	job-test

slack_slackpkg_test_SOURCES = \
	definitions.cc \
	slackpkg-test.cc
slack_slackpkg_test_LDADD = $(PK_BACKEND_SLACK_LIBS)
slack_slackpkg_test_CPPFLAGS = $(AM_CPPFLAGS)

slack_dl_test_SOURCES = \
	definitions.cc \
	dl-test.cc
slack_dl_test_LDADD = $(PK_BACKEND_SLACK_LIBS)
slack_dl_test_CPPFLAGS = $(AM_CPPFLAGS)

job_test_SOURCES = \
	definitions.cc \
	job-test.cc
job_test_LDADD = $(PK_BACKEND_SLACK_LIBS)
job_test_CPPFLAGS = $(AM_CPPFLAGS)

TESTS = $(check_PROGRAMS)

-include $(top_srcdir)/git.mk
