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
	$(NULL)

pkcon_SOURCES =						\
	pk-console.h					\
	pk-console.c					\
	pk-console-main.c				\
	$(NULL)

pkcon_LDADD =						\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(PK_LIBS)					\
	$(NULL)

pkmon_SOURCES =						\
	pk-monitor.c					\
	$(NULL)

pkmon_LDADD =						\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(PK_LIBS)					\
	$(NULL)

check_PROGRAMS =					\
	pk-self-test

noinst_PROGRAMS =					\
	pk-self-test

pk_self_test_SOURCES =					\
	pk-console.h					\
	pk-console.c					\
	pk-self-test.c					\
	$(NULL)

pk_self_test_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(SELFTEST_LIBS)				\
	$(PK_LIBS)					\
	$(NULL)

pk_self_test_CPPFLAGS=	\
	-DPK_BUILD_TESTS

clean-local:
	rm -f *~

CLEANFILES = $(BUILT_SOURCES)

TESTS = pk-self-test

DISTCLEANFILES =					\
	$(NULL)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

