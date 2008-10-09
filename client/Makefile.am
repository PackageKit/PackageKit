## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	-DBINDIR=\"$(bindir)\"			 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DVERSION="\"$(VERSION)\"" 			\
	-DPK_DATA=\"$(pkgdatadir)\"			\
	-DPK_DB_DIR=\""$(PK_DB_DIR)"\" 			\
	-DEGG_LOG_FILE=\""$(PK_LOG_DIR)/PackageKit"\"	\
	-DEGG_VERBOSE="\"PK_VERBOSE\""			\
	-DEGG_LOGGING="\"PK_LOGGING\""			\
	-DEGG_CONSOLE="\"PK_CONSOLE\""			\
	-I$(top_srcdir)/libpackagekit			\
	$(NULL)

PK_LIBS =						\
	$(top_builddir)/libpackagekit/libpackagekit.la	\
	$(NULL)

bin_PROGRAMS =						\
	pkcon						\
	pkmon						\
	pkgenpack					\
	$(NULL)

libexec_PROGRAMS =					\
	pk-import-specspo				\
	$(NULL)

pkcon_SOURCES =						\
	egg-debug.c					\
	egg-debug.h					\
	egg-string.c					\
	egg-string.h					\
	pk-console.c					\
	pk-tools-common.c				\
	pk-tools-common.h				\
	$(NULL)

pkcon_LDADD =						\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(PK_LIBS)					\
	$(NULL)

pkmon_SOURCES =						\
	egg-debug.c					\
	egg-debug.h					\
	pk-monitor.c					\
	$(NULL)

pkmon_LDADD =						\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(PK_LIBS)					\
	$(NULL)

pkgenpack_SOURCES =					\
	egg-debug.c					\
	egg-debug.h					\
	egg-string.c					\
	egg-string.h					\
	pk-generate-pack.c				\
	pk-tools-common.c				\
	pk-tools-common.h				\
	$(NULL)

pkgenpack_LDADD = 					\
	$(ARCHIVE_LIBS)					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(PK_LIBS)					\
	$(NULL)

pk_import_specspo_SOURCES =				\
	egg-debug.c					\
	egg-debug.h					\
	egg-string.c					\
	egg-string.h					\
	pk-import-common.c				\
	pk-import-common.h				\
	pk-import-specspo.c				\
	$(NULL)

pk_import_specspo_LDADD =				\
	$(GLIB_LIBS)					\
	$(SQLITE_LIBS)					\
	$(PK_LIBS)					\
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
	pk-self-test.c					\
	$(shared_SOURCES)				\
	$(NULL)

pk_self_test_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(SELFTEST_LIBS)				\
	$(ARCHIVE_LIBS)					\
	$(PK_LIBS)					\
	$(NULL)

pk_self_test_CFLAGS = -DEGG_TEST $(AM_CFLAGS)

TESTS = pk-self-test
endif

clean-local:
	rm -f *~
	rm -f *.out

CLEANFILES = $(BUILT_SOURCES)

DISTCLEANFILES =					\
	$(NULL)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

