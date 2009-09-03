## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

EXTRA_DIST =						\
	pkcon.xml					\
	pkmon.xml					\
	pkgenpack.xml					\
	$(NULL)

if HAVE_XSLTPROC
man_MANS =						\
	pkcon.1						\
	pkmon.1						\
	pkgenpack.1					\
	$(NULL)
endif

if HAVE_XSLTPROC
pkcon.1: pkcon.xml
	xsltproc http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $? &> /dev/null

pkmon.1: pkmon.xml
	xsltproc http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $? &> /dev/null

pkgenpack.1: pkgenpack.xml
	xsltproc http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $? &> /dev/null
endif

INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	-DBINDIR=\"$(bindir)\"			 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DVERSION="\"$(VERSION)\"" 			\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"		\
	-DPK_DATA=\"$(pkgdatadir)\"			\
	-DPK_DB_DIR=\""$(PK_DB_DIR)"\" 			\
	-DEGG_LOG_FILE=\""$(PK_LOG_DIR)/PackageKit"\"	\
	-DEGG_VERBOSE="\"PK_VERBOSE\""			\
	-DEGG_LOGGING="\"PK_LOGGING\""			\
	-DEGG_CONSOLE="\"PK_CONSOLE\""			\
	-I$(top_srcdir)/lib				\
	$(NULL)

PK_GLIB_LIBS =						\
	$(top_builddir)/lib/packagekit-glib/libpackagekit-glib.la	\
	$(NULL)

bin_PROGRAMS =						\
	pkcon						\
	pkmon						\
	pkgenpack					\
	$(NULL)

# Only build this functionality if we have glib2, and don't yet install it
if PK_BUILD_GLIB2

PK_GLIB2_LIBS =						\
	$(top_builddir)/lib/packagekit-glib2/libpackagekit-glib2.la	\
	$(NULL)

noinst_PROGRAMS = pkcon-test

# TODO: private library

pkcon_test_SOURCES =					\
	egg-debug.c					\
	egg-debug.h					\
	egg-string.c					\
	egg-string.h					\
	pk-console-test.c				\
	pk-task-text.c					\
	pk-task-text.h					\
	pk-text.c					\
	pk-text.h					\
	pk-progress-bar.c				\
	pk-progress-bar.h				\
	$(NULL)

pkcon_test_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(PK_GLIB2_LIBS)				\
	$(NULL)

pkcon_test_CFLAGS =					\
	$(WARNINGFLAGS_C)				\
	$(NULL)
endif

pkcon_SOURCES =						\
	egg-debug.c					\
	egg-debug.h					\
	egg-string.c					\
	egg-string.h					\
	pk-console.c					\
	pk-text.c					\
	pk-text.h					\
	pk-tools-common.c				\
	pk-tools-common.h				\
	$(NULL)

pkcon_LDADD =						\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(PK_GLIB_LIBS)					\
	$(NULL)

pkcon_CFLAGS =						\
	$(WARNINGFLAGS_C)				\
	$(NULL)

pkmon_SOURCES =						\
	egg-debug.c					\
	egg-debug.h					\
	pk-monitor.c					\
	$(NULL)

pkmon_LDADD =						\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(PK_GLIB_LIBS)					\
	$(NULL)

pkmon_CFLAGS =						\
	$(WARNINGFLAGS_C)				\
	$(NULL)

pkgenpack_SOURCES =					\
	egg-debug.c					\
	egg-debug.h					\
	egg-string.c					\
	egg-string.h					\
	pk-generate-pack.c				\
	pk-text.c					\
	pk-text.h					\
	pk-tools-common.c				\
	pk-tools-common.h				\
	$(NULL)

pkgenpack_LDADD = 					\
	$(ARCHIVE_LIBS)					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(PK_GLIB_LIBS)					\
	$(NULL)

pkgenpack_CFLAGS =					\
	$(WARNINGFLAGS_C)				\
	$(NULL)

if EGG_BUILD_TESTS

check_PROGRAMS =					\
	pk-self-test

pk_self_test_SOURCES =					\
	egg-debug.c					\
	egg-debug.h					\
	egg-string.c					\
	egg-string.h					\
	egg-test.c					\
	egg-test.h					\
	pk-text.c					\
	pk-text.h					\
	pk-task-text.c					\
	pk-task-text.h					\
	pk-self-test.c					\
	$(NULL)

pk_self_test_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(SELFTEST_LIBS)				\
	$(ARCHIVE_LIBS)					\
	$(PK_GLIB2_LIBS)				\
	$(NULL)

pk_self_test_CFLAGS = -DEGG_TEST $(AM_CFLAGS)

TESTS = pk-self-test
endif

clean-local:
	rm -f *~
	rm -f *.1
	rm -f manpage.*
	rm -f *.out

CLEANFILES = $(BUILT_SOURCES)

DISTCLEANFILES =					\
	$(NULL)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

