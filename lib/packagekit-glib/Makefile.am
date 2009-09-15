
NULL =

INCLUDES = \
	$(GLIB_CFLAGS)						\
	$(DBUS_CFLAGS)						\
	$(POLKIT_CFLAGS)					\
	$(SQLITE_CFLAGS)					\
	-I$(top_srcdir)/lib					\
	-I$(top_builddir)/lib					\
	-I$(top_builddir)/lib/packagekit-glib			\
	-I.							\
	-DPK_COMPILATION					\
	-DPK_DB_DIR=\""$(PK_DB_DIR)"\" 				\
	-DEGG_LOG_FILE=\""$(PK_LOG_DIR)/PackageKit"\"		\
	-DEGG_VERBOSE="\"PK_VERBOSE\""				\
	-DEGG_LOGGING="\"PK_LOGGING\""				\
	-DEGG_CONSOLE="\"PK_CONSOLE\""				\
	-DLOCALSTATEDIR=\""$(localstatedir)"\" 			\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"			\
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\"

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = 						\
	packagekit-glib.pc					\
	$(NULL)

lib_LTLIBRARIES =						\
	libpackagekit-glib.la					\
	$(NULL)

libpackagekit_glib_includedir = $(includedir)/PackageKit/packagekit-glib

libpackagekit_glib_include_HEADERS =				\
	packagekit.h						\
	pk-bitfield.h						\
	pk-catalog.h						\
	pk-category-obj.h					\
	pk-client.h						\
	pk-client-pool.h					\
	pk-client-sync.h					\
	pk-common.h						\
	pk-connection.h						\
	pk-control.h						\
	pk-control-sync.h					\
	pk-desktop.h						\
	pk-details-obj.h					\
	pk-distro-upgrade-obj.h					\
	pk-enum.h						\
	pk-obj-list.h						\
	pk-package-id.h						\
	pk-package-ids.h					\
	pk-package-list.h					\
	pk-package-obj.h					\
	pk-require-restart-obj.h				\
	pk-service-pack.h					\
	pk-task-list.h						\
	pk-transaction-obj.h					\
	pk-update-detail-obj.h					\
	pk-version.h						\
	$(NULL)

libpackagekit_glib_la_SOURCES =					\
	packagekit.h						\
	egg-debug.c						\
	egg-debug.h						\
	egg-string.c						\
	egg-string.h						\
	pk-version.h						\
	pk-marshal.c						\
	pk-marshal.h						\
	pk-desktop.c						\
	pk-desktop.h						\
	pk-connection.c						\
	pk-connection.h						\
	pk-service-pack.c					\
	pk-service-pack.h					\
	pk-package-id.c						\
	pk-package-id.h						\
	pk-package-ids.c					\
	pk-package-ids.h					\
	pk-package-obj.c					\
	pk-package-obj.h					\
	pk-obj-list.c						\
	pk-obj-list.h						\
	pk-package-list.c					\
	pk-package-list.h					\
	pk-category-obj.c					\
	pk-category-obj.h					\
	pk-update-detail-obj.c					\
	pk-update-detail-obj.h					\
	pk-distro-upgrade-obj.c					\
	pk-distro-upgrade-obj.h					\
	pk-require-restart-obj.c				\
	pk-require-restart-obj.h				\
	pk-details-obj.c					\
	pk-details-obj.h					\
	pk-transaction-obj.c					\
	pk-transaction-obj.h					\
	pk-enum.c						\
	pk-enum.h						\
	pk-bitfield.c						\
	pk-bitfield.h						\
	pk-common.c						\
	pk-common.h						\
	pk-client.c						\
	pk-client.h						\
	pk-client-pool.c					\
	pk-client-pool.h					\
	pk-catalog.c						\
	pk-catalog.h						\
	pk-control.c						\
	pk-control.h						\
	pk-task-list.c						\
	pk-task-list.h						\
	$(NULL)

libpackagekit_glib_la_LIBADD =					\
	$(GLIB_LIBS)						\
	$(DBUS_LIBS)						\
	$(POLKIT_LIBS)						\
	$(SQLITE_LIBS)						\
	$(ARCHIVE_LIBS)						\
	$(NULL)

libpackagekit_glib_la_LDFLAGS =					\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)	\
	-export-dynamic						\
	-no-undefined						\
	-export-symbols-regex '^pk_.*'				\
	$(NULL)

libpackagekit_glib_la_CFLAGS =					\
	$(WARNINGFLAGS_C)					\
	$(NULL)

if EGG_BUILD_TESTS
check_PROGRAMS =						\
	pk-self-test

pk_self_test_SOURCES =						\
	$(libpackagekit_glib_la_SOURCES)			\
	egg-test.h						\
	egg-test.c						\
	pk-self-test.c						\
	$(NULL)

pk_self_test_LDADD =						\
	$(GLIB_LIBS)						\
	$(DBUS_LIBS)						\
	$(SELFTEST_LIBS)					\
	$(POLKIT_LIBS)						\
	$(SQLITE_LIBS)						\
	$(ARCHIVE_LIBS)						\
	$(NULL)

pk_self_test_CFLAGS = -DEGG_TEST $(AM_CFLAGS)

TESTS = pk-self-test
endif

EXTRA_DIST =							\
	packagekit-glib.pc.in					\
	pk-version.h.in						\
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

CLEANFILES = $(BUILT_SOURCES)

clean-local:
	rm -f *~
	rm -f $(CLEANFILES)

