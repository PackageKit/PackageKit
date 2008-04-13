
NULL =

INCLUDES = \
	$(GLIB_CFLAGS)						\
	$(DBUS_CFLAGS)						\
	$(LIBNM_CFLAGS)						\
	$(SQLITE_CFLAGS)					\
	-I$(top_srcdir)/libpackagekit				\
	-I$(top_srcdir)/libgbus					\
	-I$(top_srcdir)/libselftest				\
	-DPK_DB_DIR=\""$(PK_DB_DIR)"\" 				\
	-DPK_LOG_DIR=\""$(PK_LOG_DIR)"\" 			\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"			\
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\"

GBUS_LIBS =							\
	$(top_builddir)/libgbus/libgbus.la			\
	$(NULL)

SELFTEST_LIBS =							\
	$(top_builddir)/libselftest/libselftest.la		\
	$(NULL)

PK_LIBS =							\
	$(top_builddir)/libpackagekit/libpackagekit.la		\
	$(NULL)

lib_LTLIBRARIES =						\
	libpackagekit.la					\
	$(NULL)

libpackagekit_includedir = $(includedir)/packagekit

libpackagekit_include_HEADERS =					\
	pk-debug.h						\
	pk-extra.h						\
	pk-connection.h						\
	pk-network.h						\
	pk-package-id.h						\
	pk-package-ids.h					\
	pk-package-list.h					\
	pk-enum.h						\
	pk-common.h						\
	pk-client.h						\
	pk-control.h						\
	pk-task-list.h						\
	pk-polkit-client.h					\
	$(NULL)

libpackagekit_la_SOURCES =					\
	pk-debug.c						\
	pk-debug.h						\
	pk-marshal.c						\
	pk-marshal.h						\
	pk-extra.c						\
	pk-extra.h						\
	pk-connection.c						\
	pk-connection.h						\
	pk-network.h						\
	pk-package-id.c						\
	pk-package-id.h						\
	pk-package-ids.c					\
	pk-package-ids.h					\
	pk-package-list.c					\
	pk-package-list.h					\
	pk-enum.h						\
	pk-enum.c						\
	pk-common.c						\
	pk-common.h						\
	pk-client.c						\
	pk-client.h						\
	pk-control.c						\
	pk-control.h						\
	pk-task-list.c						\
	pk-task-list.h						\
	pk-polkit-client.c					\
	pk-polkit-client.h					\
	$(NULL)

if PK_BUILD_NETWORKMANAGER
libpackagekit_la_SOURCES +=					\
	pk-network-nm.c						\
	$(NULL)
else
libpackagekit_la_SOURCES +=					\
	pk-network-dummy.c					\
	$(NULL)
endif

libpackagekit_la_LIBADD =					\
	$(GLIB_LIBS)						\
	$(DBUS_LIBS)						\
	$(GBUS_LIBS)						\
	$(LIBNM_LIBS)						\
	$(SQLITE_LIBS)						\
	$(NULL)

if PK_BUILD_TESTS
libpackagekit_la_LIBADD +=					\
	$(SELFTEST_LIBS)					\
	$(NULL)
endif

libpackagekit_la_LDFLAGS =					\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)	\
	$(NULL)

if PK_BUILD_TESTS
check_PROGRAMS =						\
	pk-self-test

pk_self_test_SOURCES =						\
	pk-self-test.c						\
	$(NULL)

pk_self_test_LDADD =						\
	$(GLIB_LIBS)						\
	$(DBUS_LIBS)						\
	$(SELFTEST_LIBS)					\
	$(PK_LIBS)						\
	$(NULL)

TESTS = pk-self-test
endif

EXTRA_DIST =							\
	pk-marshal.list

BUILT_SOURCES = 						\
	pk-marshal.c						\
	pk-marshal.h						\
	$(NULL)

pk-marshal.c: pk-marshal.list
	echo "#include \"pk-marshal.h\"" > $@ && \
	@GLIB_GENMARSHAL@ $< --prefix=pk_marshal --body >> $@

pk-marshal.h: pk-marshal.list
	@GLIB_GENMARSHAL@ $< --prefix=pk_marshal --header > $@

if PK_BUILD_GCOV
clean-gcov:
	rm -f *.gcov *.gcda
gcov: clean-gcov all check
	$(top_srcdir)/tools/create-coverage-report.sh libpackagekit $(filter %.c,$(libpackagekit_la_SOURCES)) > gcov.txt
endif

if PK_BUILD_GPROF
clean-gprof:
	rm -f *.out
gprof: clean-gprof all check
	gprof .libs/pk-self-test > gprof.txt
endif

CLEANFILES = $(BUILT_SOURCES)

clean-local:
	rm -f *~
	rm -f *.gcno
	rm -f *.out
	rm -f gcov.txt
	rm -f gprof.txt
	rm -f $(CLEANFILES)

