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
	-I$(top_srcdir)/libpackagekit			\
	-I$(top_srcdir)/libselftest			\
	$(NULL)

SELFTEST_LIBS =						\
	$(top_builddir)/libselftest/libselftest.la	\
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
	pk-import-desktop				\
	pk-import-specspo				\
	pk-generate-package-list			\
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
	$(SELFTEST_LIBS)				\
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
	$(SELFTEST_LIBS)				\
	$(NULL)

pkgenpack_SOURCES =					\
	egg-debug.c					\
	egg-debug.h					\
	egg-string.c					\
	egg-string.h					\
	pk-generate-pack-main.c				\
	pk-generate-pack.c				\
	pk-generate-pack.h				\
	pk-tools-common.c				\
	pk-tools-common.h				\
	$(NULL)

pkgenpack_LDADD = 					\
	$(ARCHIVE_LIBS)					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(PK_LIBS)					\
	$(SELFTEST_LIBS)				\
	$(NULL)

pk_generate_package_list_SOURCES =			\
	egg-debug.c					\
	egg-debug.h					\
	pk-generate-package-list.c			\
	$(NULL)

pk_generate_package_list_LDADD =			\
	$(GLIB_LIBS)					\
	$(PK_LIBS)					\
	$(NULL)

pk_import_desktop_SOURCES =				\
	egg-debug.c					\
	egg-debug.h					\
	egg-string.c					\
	egg-string.h					\
	pk-import-common.c				\
	pk-import-common.h				\
	pk-import-desktop.c				\
	$(NULL)

pk_import_desktop_LDADD =				\
	$(GLIB_LIBS)					\
	$(SQLITE_LIBS)					\
	$(PK_LIBS)					\
	$(SELFTEST_LIBS)				\
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

if PK_BUILD_TESTS

check_PROGRAMS =					\
	pk-self-test

pk_self_test_SOURCES =					\
	egg-debug.c					\
	egg-debug.h					\
	egg-string.c					\
	egg-string.h					\
	pk-self-test.c					\
	pk-generate-pack.c				\
	pk-tools-common.c				\
	pk-tools-common.h				\
	$(shared_SOURCES)				\
	$(NULL)

pk_self_test_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(SELFTEST_LIBS)				\
	$(ARCHIVE_LIBS)					\
	$(PK_LIBS)					\
	$(NULL)

TESTS = pk-self-test
endif

clean-local:
	rm -f *~
	rm -f *.out
	rm -f *.gcno
	rm -f *.gcda
	rm -f gcov.txt
	rm -f gprof.txt

CLEANFILES = $(BUILT_SOURCES)

DISTCLEANFILES =					\
	$(NULL)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

