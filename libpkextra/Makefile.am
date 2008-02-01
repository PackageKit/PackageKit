
NULL =

INCLUDES = \
	$(GLIB_CFLAGS)						\
	$(DBUS_CFLAGS)						\
	$(LIBNM_CFLAGS)						\
	$(SQLITE_CFLAGS)					\
	-I$(top_srcdir)/libpackagekit				\
	-I$(top_srcdir)/libselftest				\
	-DPK_DB_DIR=\""$(PK_DB_DIR)"\" 				\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"			\
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\"

SELFTEST_LIBS =							\
	$(top_builddir)/libselftest/libselftest.la		\
	$(NULL)

PK_LIBS =							\
	$(top_builddir)/libpackagekit/libpackagekit.la		\
	$(NULL)

PKEXTRA_LIBS =							\
	$(top_builddir)/libpkextra/libpkextra.la		\
	$(NULL)

lib_LTLIBRARIES =						\
	libpkextra.la						\
	$(NULL)

libpkextra_includedir = $(includedir)/packagekit

libpkextra_include_HEADERS =					\
	pk-extra.h						\
	$(NULL)

libpkextra_la_SOURCES =						\
	pk-extra.c						\
	pk-extra.h						\
	$(NULL)

libpkextra_la_LIBADD =						\
	$(GLIB_LIBS)						\
	$(GBUS_LIBS)						\
	$(SQLITE_LIBS)						\
	$(PK_LIBS)						\
	$(NULL)

libpkextra_la_LDFLAGS =						\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)	\
	$(NULL)

if PK_BUILD_TESTS
libpkextra_la_LIBADD +=						\
	$(SELFTEST_LIBS)					\
	$(NULL)
endif

if PK_BUILD_TESTS
check_PROGRAMS =						\
	pk-self-test

noinst_PROGRAMS =						\
	pk-self-test

pk_self_test_SOURCES =						\
	pk-self-test.c						\
	$(NULL)

pk_self_test_LDADD =						\
	$(GLIB_LIBS)						\
	$(DBUS_LIBS)						\
	$(SELFTEST_LIBS)					\
	$(PKEXTRA_LIBS)						\
	$(SQLITE_LIBS)						\
	$(NULL)

TESTS = pk-self-test
endif

CLEANFILES = $(BUILT_SOURCES)

clean-local:
	rm -f *~
	rm -f *.gcno
	rm -f *.out
	rm -f gcov.txt
	rm -f gprof.txt
	rm -f $(CLEANFILES)

