NULL =

INCLUDES = \
	$(GLIB_CFLAGS)						\
	$(DBUS_CFLAGS)						\
	$(POLKIT_CFLAGS)					\
	$(SQLITE_CFLAGS)					\
	-I$(top_srcdir)/lib					\
	-I$(top_builddir)/lib					\
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
	packagekit-glib2.pc					\
	$(NULL)

lib_LTLIBRARIES =						\
	libpackagekit-glib2.la					\
	$(NULL)

libpackagekit_glib2_includedir = $(includedir)/PackageKit/packagekit-glib2

libpackagekit_glib2_include_HEADERS =				\
	packagekit.h						\
	pk-bitfield.h						\
	pk-client.h						\
	pk-common.h						\
	pk-control.h						\
	pk-desktop.h						\
	pk-enum.h						\
	pk-item.h						\
	pk-package.h						\
	pk-package-id.h						\
	pk-package-ids.h					\
	pk-package-sack.h					\
	pk-progress.h						\
	pk-results.h						\
	pk-service-pack.h					\
	pk-task.h						\
	$(NULL)

libpackagekit_glib2_la_SOURCES =				\
	egg-debug.c						\
	egg-debug.h						\
	egg-string.c						\
	egg-string.h						\
	packagekit.h						\
	pk-bitfield.c						\
	pk-bitfield.h						\
	pk-client.c						\
	pk-client.h						\
	pk-common.c						\
	pk-common.h						\
	pk-control.c						\
	pk-control.h						\
	pk-desktop.c						\
	pk-desktop.h						\
	pk-enum.c						\
	pk-enum.h						\
	pk-marshal.c						\
	pk-marshal.h						\
	pk-package.c						\
	pk-package.h						\
	pk-package-id.c						\
	pk-package-id.h						\
	pk-package-ids.c					\
	pk-package-ids.h					\
	pk-package-sack.c					\
	pk-package-sack.h					\
	pk-progress.c						\
	pk-progress.h						\
	pk-service-pack.c					\
	pk-service-pack.h					\
	pk-results.c						\
	pk-results.h						\
	pk-item.c						\
	pk-item.h						\
	pk-task.c						\
	pk-task.h						\
	pk-version.h						\
	$(NULL)

libpackagekit_glib2_la_LIBADD =					\
	$(GLIB_LIBS)						\
	$(GIO_LIBS)						\
	$(DBUS_LIBS)						\
	$(ARCHIVE_LIBS)						\
	$(SQLITE_LIBS)						\
	$(NULL)

libpackagekit_glib2_la_LDFLAGS =				\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)	\
	-export-dynamic						\
	-no-undefined						\
	-export-symbols-regex '^pk_.*'				\
	$(NULL)

libpackagekit_glib2_la_CFLAGS =					\
	$(WARNINGFLAGS_C)					\
	$(NULL)

noinst_LIBRARIES = libpackagekitprivate.a
libpackagekitprivate_a_SOURCES =				\
	egg-debug.c						\
	egg-debug.h						\
	egg-string.c						\
	egg-string.h						\
	packagekit-private.h					\
	pk-client-sync.c					\
	pk-client-sync.h					\
	pk-console-shared.c					\
	pk-console-shared.h					\
	pk-progress-bar.c					\
	pk-progress-bar.h					\
	pk-task-sync.c						\
	pk-task-sync.h						\
	pk-task-text.c						\
	pk-task-text.h						\
	pk-task-wrapper.c					\
	pk-task-wrapper.h					\
	$(NULL)

libpackagekitprivate_a_CFLAGS =					\
	$(WARNINGFLAGS_C)					\
	$(NULL)

if EGG_BUILD_TESTS
check_PROGRAMS =						\
	pk-self-test

pk_self_test_SOURCES =						\
	$(libpackagekit_glib2_la_SOURCES)			\
	egg-test.h						\
	egg-test.c						\
	pk-task-wrapper.c					\
	pk-task-wrapper.h					\
	pk-task-text.c						\
	pk-task-text.h						\
	pk-console-shared.c					\
	pk-console-shared.h					\
	pk-client-sync.c					\
	pk-client-sync.h					\
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

pk_self_test_CFLAGS = -DEGG_TEST $(AM_CFLAGS) $(WARNINGFLAGS_C)

TESTS = pk-self-test
endif

EXTRA_DIST =							\
	packagekit-glib2.pc.in					\
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

