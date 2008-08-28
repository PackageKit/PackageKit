
NULL =

INCLUDES = \
	$(GLIB_CFLAGS)						\
	$(DBUS_CFLAGS)						\
	$(POLKIT_CFLAGS)					\
	$(SQLITE_CFLAGS)					\
	-I$(top_srcdir)/libpackagekit				\
	-I$(top_srcdir)/libselftest				\
	-DPK_DB_DIR=\""$(PK_DB_DIR)"\" 				\
	-DEGG_LOG_FILE=\""$(PK_LOG_DIR)/PackageKit"\"		\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"			\
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\"

SELFTEST_LIBS =							\
	$(top_builddir)/libselftest/libselftest.la		\
	$(NULL)

lib_LTLIBRARIES =						\
	libpackagekit.la					\
	$(NULL)

libpackagekit_includedir = $(includedir)/packagekit

libpackagekit_include_HEADERS =					\
	pk-extra.h						\
	pk-connection.h						\
	pk-package-id.h						\
	pk-package-ids.h					\
	pk-package-obj.h					\
	pk-package-list.h					\
	pk-update-detail-obj.h					\
	pk-update-detail-list.h					\
	pk-distro-upgrade-obj.h					\
	pk-details-obj.h					\
	pk-enum.h						\
	pk-bitfield.h						\
	pk-common.h						\
	pk-client.h						\
	pk-catalog.h						\
	pk-control.h						\
	pk-task-list.h						\
	$(NULL)

libpackagekit_la_SOURCES =					\
	egg-debug.c						\
	egg-debug.h						\
	egg-dbus-monitor.c					\
	egg-dbus-monitor.h					\
	egg-obj-list.c						\
	egg-obj-list.h						\
	pk-marshal.c						\
	pk-marshal.h						\
	pk-extra.c						\
	pk-extra.h						\
	pk-connection.c						\
	pk-connection.h						\
	pk-package-id.c						\
	pk-package-id.h						\
	pk-package-ids.c					\
	pk-package-ids.h					\
	pk-package-obj.c					\
	pk-package-obj.h					\
	pk-package-list.c					\
	pk-package-list.h					\
	pk-update-detail-obj.c					\
	pk-update-detail-obj.h					\
	pk-update-detail-list.c					\
	pk-update-detail-list.h					\
	pk-distro-upgrade-obj.c					\
	pk-distro-upgrade-obj.h					\
	pk-details-obj.c					\
	pk-details-obj.h					\
	pk-enum.c						\
	pk-enum.h						\
	pk-bitfield.c						\
	pk-bitfield.h						\
	pk-common.c						\
	pk-common.h						\
	pk-client.c						\
	pk-client.h						\
	pk-catalog.c						\
	pk-catalog.h						\
	pk-control.c						\
	pk-control.h						\
	pk-task-list.c						\
	pk-task-list.h						\
	$(NULL)

libpackagekit_la_LIBADD =					\
	$(GLIB_LIBS)						\
	$(DBUS_LIBS)						\
	$(POLKIT_LIBS)						\
	$(SQLITE_LIBS)						\
	$(NULL)

if PK_BUILD_TESTS
libpackagekit_la_LIBADD +=					\
	$(SELFTEST_LIBS)					\
	$(NULL)
endif

libpackagekit_la_LDFLAGS =					\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)	\
	-export-dynamic						\
	-no-undefined						\
	-export-symbols-regex '^pk_.*'				\
	$(NULL)

if PK_BUILD_TESTS
check_PROGRAMS =						\
	pk-self-test

pk_self_test_SOURCES =						\
	$(libpackagekit_la_SOURCES)				\
	pk-self-test.c						\
	$(NULL)

pk_self_test_LDADD =						\
	$(GLIB_LIBS)						\
	$(DBUS_LIBS)						\
	$(SELFTEST_LIBS)					\
	$(POLKIT_LIBS)						\
	$(SQLITE_LIBS)						\
	$(NULL)

pk_self_test_CFLAGS = $(AM_CFLAGS)

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

