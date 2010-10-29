NULL =

if HAVE_INTROSPECTION
-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir)
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)
endif

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
	-DG_LOG_DOMAIN=\"PackageKit\"				\
	-DLOCALSTATEDIR=\""$(localstatedir)"\" 			\
	-DTESTDATADIR=\""$(top_srcdir)/data/tests"\"		\
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
	pk-catalog.h						\
	pk-category.h						\
	pk-client.h						\
	pk-client-helper.h					\
	pk-client-sync.h					\
	pk-common.h						\
	pk-control.h						\
	pk-control-sync.h					\
	pk-desktop.h						\
	pk-details.h						\
	pk-distro-upgrade.h					\
	pk-enum.h						\
	pk-error.h						\
	pk-eula-required.h					\
	pk-files.h						\
	pk-media-change-required.h				\
	pk-message.h						\
	pk-package.h						\
	pk-package-id.h						\
	pk-package-ids.h					\
	pk-package-sack.h					\
	pk-package-sack-sync.h					\
	pk-progress.h						\
	pk-repo-detail.h					\
	pk-repo-signature-required.h				\
	pk-require-restart.h					\
	pk-results.h						\
	pk-service-pack.h					\
	pk-source.h						\
	pk-task.h						\
	pk-transaction-past.h					\
	pk-transaction-list.h					\
	pk-update-detail.h					\
	pk-version.h						\
	$(NULL)

libpackagekit_glib2_la_SOURCES =				\
	packagekit.h						\
	pk-bitfield.c						\
	pk-bitfield.h						\
	pk-catalog.c						\
	pk-catalog.h						\
	pk-category.c						\
	pk-category.h						\
	pk-client.c						\
	pk-client.h						\
	pk-client-helper.c					\
	pk-client-helper.h					\
	pk-client-sync.c					\
	pk-client-sync.h					\
	pk-common.c						\
	pk-common.h						\
	pk-control.c						\
	pk-control.h						\
	pk-control-sync.c					\
	pk-control-sync.h					\
	pk-debug.c						\
	pk-debug.h						\
	pk-desktop.c						\
	pk-desktop.h						\
	pk-details.c						\
	pk-details.h						\
	pk-distro-upgrade.c					\
	pk-distro-upgrade.h					\
	pk-enum.c						\
	pk-enum.h						\
	pk-error.c						\
	pk-error.h						\
	pk-eula-required.c					\
	pk-eula-required.h					\
	pk-files.c						\
	pk-files.h						\
	pk-marshal.c						\
	pk-marshal.h						\
	pk-media-change-required.c				\
	pk-media-change-required.h				\
	pk-message.c						\
	pk-message.h						\
	pk-package.c						\
	pk-package.h						\
	pk-package-id.c						\
	pk-package-id.h						\
	pk-package-ids.c					\
	pk-package-ids.h					\
	pk-package-sack.c					\
	pk-package-sack.h					\
	pk-package-sack-sync.c					\
	pk-package-sack-sync.h					\
	pk-progress.c						\
	pk-progress.h						\
	pk-repo-detail.c					\
	pk-repo-detail.h					\
	pk-repo-signature-required.c				\
	pk-repo-signature-required.h				\
	pk-require-restart.c					\
	pk-require-restart.h					\
	pk-results.c						\
	pk-results.h						\
	pk-service-pack.c					\
	pk-service-pack.h					\
	pk-source.c						\
	pk-source.h						\
	pk-task.c						\
	pk-task.h						\
	pk-transaction-past.c					\
	pk-transaction-past.h					\
	pk-transaction-list.c					\
	pk-transaction-list.h					\
	pk-update-detail.c					\
	pk-update-detail.h					\
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
	packagekit-private.h					\
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
	pk-self-test.c						\
	$(NULL)

pk_self_test_LDADD =						\
	libpackagekit-glib2.la					\
	libpackagekitprivate.a					\
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

if HAVE_INTROSPECTION
introspection_sources = $(libpackagekit_glib2_la_SOURCES)

PackageKitGlib-1.0.gir: libpackagekit-glib2.la
PackageKitGlib_1_0_gir_INCLUDES = GObject-2.0 Gio-2.0
PackageKitGlib_1_0_gir_CFLAGS = $(INCLUDES) -DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
PackageKitGlib_1_0_gir_SCANNERFLAGS = --identifier-prefix=Pk --symbol-prefix=pk_ --warn-all --add-include-path=$(srcdir)
PackageKitGlib_1_0_gir_LIBS = libpackagekit-glib2.la
PackageKitGlib_1_0_gir_FILES = $(introspection_sources)
INTROSPECTION_GIRS += PackageKitGlib-1.0.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)
endif

clean-local:
	rm -f *~
	rm -f $(CLEANFILES)

