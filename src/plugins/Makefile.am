## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

PK_GLIB2_LIBS =						\
	$(top_builddir)/lib/packagekit-glib2/libpackagekit-glib2.la

INCLUDES =						\
	$(GIO_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	-DBINDIR=\"$(bindir)\"				\
	-DDATADIR=\"$(datadir)\"			\
	-DG_LOG_DOMAIN=\"PkPlugin\"			\
	-DLIBDIR=\""$(libdir)"\"			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\"		\
	-DPK_COMPILATION				\
	-DSBINDIR=\"$(sbindir)\"			\
	-DSYSCONFDIR=\""$(sysconfdir)"\"		\
	-DTESTDATADIR=\""$(top_srcdir)/data/tests"\"	\
	-I$(top_srcdir)/lib

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = 					\
	packagekit-plugin.pc

if EGG_BUILD_TESTS

check_PROGRAMS =					\
	pk-self-test

pk_self_test_SOURCES =					\
	pk-self-test.c					\
	pk-lsof.h					\
	pk-lsof.c

pk_self_test_LDADD =					\
	$(GLIB_LIBS)					\
	$(GMODULE_LIBS)					\
	$(SQLITE_LIBS)					\
	$(PK_GLIB2_LIBS)				\
	$(GIO_LIBS)

pk_self_test_CFLAGS = -DEGG_TEST $(AM_CFLAGS)

TESTS = pk-self-test
endif

clean-local:
	rm -f *~

# plugins
plugindir = $(LIBDIR)/packagekit-plugins
noinst_LTLIBRARIES =					\
	libpk_plugin_example.la

plugin_LTLIBRARIES =					\
	libpk_plugin_scripts.la				\
	libpk_plugin-update-package-cache.la		\
	libpk_plugin-no-update-process.la		\
	libpk_plugin-scan-desktop-files.la		\
	libpk_plugin-systemd-updates.la			\
	libpk_plugin-update-check-processes.la		\
	libpk_plugin-check-shared-libraries-in-use.la	\
	libpk_plugin-clear-system-update.la		\
	libpk_plugin-clear-firmware-requests.la

libpk_plugin_example_la_SOURCES = pk-plugin-example.c
libpk_plugin_example_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_plugin_example_la_LDFLAGS = -module -avoid-version
libpk_plugin_example_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libpk_plugin_systemd_updates_la_SOURCES = pk-plugin-systemd-updates.c
libpk_plugin_systemd_updates_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_plugin_systemd_updates_la_LDFLAGS = -module -avoid-version
libpk_plugin_systemd_updates_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libpk_plugin_scripts_la_SOURCES = pk-plugin-scripts.c
libpk_plugin_scripts_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_plugin_scripts_la_LDFLAGS = -module -avoid-version
libpk_plugin_scripts_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libpk_plugin_clear_firmware_requests_la_SOURCES = pk-plugin-clear-firmware-requests.c
libpk_plugin_clear_firmware_requests_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_plugin_clear_firmware_requests_la_LDFLAGS = -module -avoid-version
libpk_plugin_clear_firmware_requests_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libpk_plugin_clear_system_update_la_SOURCES = pk-plugin-clear-system-update.c
libpk_plugin_clear_system_update_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_plugin_clear_system_update_la_LDFLAGS = -module -avoid-version
libpk_plugin_clear_system_update_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libpk_plugin_update_package_cache_la_SOURCES = \
						pk-plugin-update-package-cache.c \
						pk-package-cache.h \
						pk-package-cache.c
libpk_plugin_update_package_cache_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_plugin_update_package_cache_la_LDFLAGS = -module -avoid-version
libpk_plugin_update_package_cache_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libpk_plugin_no_update_process_la_SOURCES =		\
	pk-plugin-no-update-process.c
libpk_plugin_no_update_process_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_plugin_no_update_process_la_LDFLAGS = -module -avoid-version
libpk_plugin_no_update_process_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libpk_plugin_scan_desktop_files_la_SOURCES = pk-plugin-scan-desktop-files.c
libpk_plugin_scan_desktop_files_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_plugin_scan_desktop_files_la_LDFLAGS = -module -avoid-version
libpk_plugin_scan_desktop_files_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libpk_plugin_update_check_processes_la_SOURCES =	\
	pk-plugin-update-check-processes.c
libpk_plugin_update_check_processes_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_plugin_update_check_processes_la_LDFLAGS = -module -avoid-version
libpk_plugin_update_check_processes_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libpk_plugin_check_shared_libraries_in_use_la_SOURCES =	\
	pk-plugin-check-shared-libraries-in-use.c	\
	pk-lsof.h					\
	pk-lsof.c
libpk_plugin_check_shared_libraries_in_use_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_plugin_check_shared_libraries_in_use_la_LDFLAGS = -module -avoid-version
libpk_plugin_check_shared_libraries_in_use_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

-include $(top_srcdir)/git.mk
