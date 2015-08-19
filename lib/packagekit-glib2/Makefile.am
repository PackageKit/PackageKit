if HAVE_INTROSPECTION
-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir)
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir)
endif

AM_CPPFLAGS = \
	$(GLIB_CFLAGS)						\
	$(GIO_CFLAGS)						\
	$(POLKIT_CFLAGS)					\
	$(SQLITE_CFLAGS)					\
	-I$(top_srcdir)						\
	-I$(top_srcdir)/lib					\
	-I$(top_builddir)/lib					\
	-I.							\
	-DPK_COMPILATION					\
	-DPK_DB_DIR=\""$(PK_DB_DIR)"\" 				\
	-DG_LOG_DOMAIN=\"PackageKit\"				\
	-DLOCALSTATEDIR=\""$(localstatedir)"\" 			\
	-DTESTDATADIR=\""$(abs_top_srcdir)/data/tests"\"		\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"			\
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\"

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = 						\
	packagekit-glib2.pc

lib_LTLIBRARIES =						\
	libpackagekit-glib2.la

libpackagekit_glib2_includedir = $(includedir)/PackageKit/packagekit-glib2

HEADER_FILES =							\
	packagekit.h						\
	pk-bitfield.h						\
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
	pk-item-progress.h					\
	pk-offline.h						\
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
	pk-source.h						\
	pk-task.h						\
	pk-task-sync.h						\
	pk-transaction-past.h					\
	pk-transaction-list.h					\
	pk-update-detail.h					\
	pk-version.h

libpackagekit_glib2_include_HEADERS =				\
	$(HEADER_FILES)						\
	pk-enum-types.h

libpackagekit_glib2_la_SOURCES =				\
	packagekit.h						\
	pk-bitfield.c						\
	pk-bitfield.h						\
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
	pk-enum-types.c						\
	pk-enum-types.h						\
	pk-error.c						\
	pk-error.h						\
	pk-eula-required.c					\
	pk-eula-required.h					\
	pk-files.c						\
	pk-files.h						\
	pk-media-change-required.c				\
	pk-media-change-required.h				\
	pk-item-progress.c					\
	pk-item-progress.h					\
	pk-offline.c						\
	pk-offline.h						\
	pk-offline-private.c					\
	pk-offline-private.h					\
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
	pk-source.c						\
	pk-source.h						\
	pk-task.c						\
	pk-task.h						\
	pk-task-sync.c						\
	pk-task-sync.h						\
	pk-transaction-past.c					\
	pk-transaction-past.h					\
	pk-transaction-list.c					\
	pk-transaction-list.h					\
	pk-update-detail.c					\
	pk-update-detail.h					\
	pk-spawn-polkit-agent.c				\
	pk-spawn-polkit-agent.h				\
	pk-version.h

libpackagekit_glib2_la_LIBADD =					\
	$(GLIB_LIBS)						\
	$(GIO_LIBS)						\
	$(ARCHIVE_LIBS)						\
	$(SQLITE_LIBS)

libpackagekit_glib2_la_LDFLAGS =				\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)	\
	-export-dynamic						\
	-no-undefined						\
	-export-symbols-regex '^pk_.*'

libpackagekit_glib2_la_CFLAGS =					\
	$(PIE_CFLAGS)						\
	$(WARNINGFLAGS_C)

noinst_LIBRARIES = libpackagekitprivate.a
libpackagekitprivate_a_SOURCES =				\
	packagekit-private.h					\
	pk-console-shared.c					\
	pk-console-shared.h					\
	pk-progress-bar.c					\
	pk-progress-bar.h					\
	pk-task-text.c						\
	pk-task-text.h						\
	pk-task-wrapper.c					\
	pk-task-wrapper.h					\
	pk-spawn-polkit-agent.h

libpackagekitprivate_a_CFLAGS =					\
	$(PIE_CFLAGS)						\
	$(WARNINGFLAGS_C)

noinst_PROGRAMS = pk-socket-example
pk_socket_example_SOURCES = pk-socket-example.c
pk_socket_example_LDADD = $(GIO_LIBS) $(GLIB_LIBS)
pk_socket_example_CFLAGS = $(WARNINGFLAGS_C)

check_PROGRAMS =						\
	pk-test-daemon						\
	pk-test-private

pk_test_private_SOURCES =					\
	$(libpackagekit_glib2_la_SOURCES)			\
	pk-test-private.c

pk_test_private_LDADD =						\
	libpackagekitprivate.a					\
	$(GLIB_LIBS)						\
	$(SELFTEST_LIBS)					\
	$(POLKIT_LIBS)						\
	$(SQLITE_LIBS)						\
	$(ARCHIVE_LIBS)

pk_test_private_CFLAGS =					\
	-DPK_OFFLINE_DESTDIR=\"/tmp/PackageKit-self-test\"	\
	$(AM_CFLAGS)						\
	$(WARNINGFLAGS_C)

pk_test_daemon_SOURCES =					\
	$(libpackagekit_glib2_la_SOURCES)			\
	pk-test-daemon.c

pk_test_daemon_LDADD =						\
	libpackagekitprivate.a					\
	$(GLIB_LIBS)						\
	$(SELFTEST_LIBS)					\
	$(POLKIT_LIBS)						\
	$(SQLITE_LIBS)						\
	$(ARCHIVE_LIBS)

pk_test_daemon_CFLAGS =						\
	$(AM_CFLAGS)						\
	$(WARNINGFLAGS_C)

TESTS = $(check_PROGRAMS)

EXTRA_DIST =							\
	packagekit-glib2.pc.in					\
	pk-version.h.in						\
	pk-enum-types.h.template				\
	pk-enum-types.c.template				\
	PackageKitGlib-1.0.metadata

BUILT_SOURCES = 						\
	pk-enum-types.h						\
	pk-enum-types.c

pk-enum-types.h: pk-enum-types.h.template $(HEADER_FILES)
	$(AM_V_GEN) $(GLIB_MKENUMS) --template $^ > $@

pk-enum-types.c: pk-enum-types.c.template $(HEADER_FILES)
	$(AM_V_GEN) $(GLIB_MKENUMS) --template $^ > $@

CLEANFILES = $(BUILT_SOURCES) *.a *.servicepack

if HAVE_INTROSPECTION
introspection_sources = $(libpackagekit_glib2_la_SOURCES)

PackageKitGlib-1.0.gir: libpackagekit-glib2.la
PackageKitGlib_1_0_gir_INCLUDES = GObject-2.0 Gio-2.0
PackageKitGlib_1_0_gir_CFLAGS = $(AM_CPPFLAGS) -DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE
PackageKitGlib_1_0_gir_SCANNERFLAGS = --identifier-prefix=Pk \
					--symbol-prefix=pk \
					--warn-all \
					--add-include-path=$(srcdir) \
					--c-include="packagekit-glib2/packagekit.h"
PackageKitGlib_1_0_gir_EXPORT_PACKAGES = packagekit-glib2
PackageKitGlib_1_0_gir_LIBS = libpackagekit-glib2.la
PackageKitGlib_1_0_gir_FILES = $(introspection_sources)
INTROSPECTION_GIRS += PackageKitGlib-1.0.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

if ENABLE_VAPIGEN
include $(VAPIGEN_MAKEFILE)

packagekit-glib2.vapi: PackageKitGlib-1.0.gir PackageKitGlib-1.0.metadata

VAPIGEN_VAPIS = packagekit-glib2.vapi

packagekit_glib2_vapi_DEPS = gio-2.0
packagekit_glib2_vapi_METADATADIRS = $(srcdir)
packagekit_glib2_vapi_FILES = PackageKitGlib-1.0.gir

vapidir = $(datadir)/vala/vapi
vapi_DATA = $(VAPIGEN_VAPIS)
endif

CLEANFILES += $(gir_DATA) $(typelib_DATA) $(VAPIGEN_VAPIS)
endif

clean-local:
	rm -f *~
	rm -f $(CLEANFILES)


-include $(top_srcdir)/git.mk
