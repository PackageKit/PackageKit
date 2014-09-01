## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

if HAVE_INTROSPECTION
-include $(INTROSPECTION_MAKEFILE)
INTROSPECTION_GIRS =
INTROSPECTION_SCANNER_ARGS = --add-include-path=$(srcdir) \
			     --add-include-path=$(top_builddir)/lib/packagekit-glib2
INTROSPECTION_COMPILER_ARGS = --includedir=$(srcdir) \
			      --includedir=$(top_builddir)/lib/packagekit-glib2
endif

SUBDIRS = plugins

PK_GLIB2_LIBS =	$(top_builddir)/lib/packagekit-glib2/libpackagekit-glib2.la

AM_CPPFLAGS =						\
	$(GIO_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	$(POLKIT_CFLAGS)				\
	$(PYTHON_CFLAGS)				\
	$(NETWORK_MANAGER_CFLAGS)			\
	$(CONNMAN_CFLAGS)				\
	$(SYSTEMD_CFLAGS)				\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"		\
	-DBINDIR=\"$(bindir)\"				\
	-DSBINDIR=\"$(sbindir)\"			\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\"			\
	-DSYSCONFDIR=\""$(sysconfdir)"\"		\
	-DLIBDIR=\""$(libdir)"\"			\
	-DLIBEXECDIR=\""$(libexecdir)"\"		\
	-DVERSION="\"$(VERSION)\""			\
	-DPK_DATA=\"$(pkgdatadir)\"			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\"		\
	-DTESTDATADIR=\""$(abs_top_srcdir)/data/tests"\"	\
	-DPK_COMPILATION				\
	-DPOLKIT_LOCAL_I_KNOW_API_IS_SUBJECT_TO_CHANGE	\
	-DG_LOG_DOMAIN=\"PackageKit\"			\
	-I$(top_builddir)/lib				\
	-I$(top_srcdir)/lib

introspectiondir = $(datadir)/dbus-1/interfaces
dist_introspection_DATA = 				\
	org.freedesktop.PackageKit.xml			\
	org.freedesktop.PackageKit.Transaction.xml

pk-resources.c: packagekit.gresource.xml $(dist_introspection_DATA)
	$(AM_V_GEN)					\
	glib-compile-resources				\
		--sourcedir=$(srcdir)			\
		--sourcedir=$(top_builddir)/data	\
		--target=$@				\
		--generate-source			\
		--c-name pk				\
		$(srcdir)/packagekit.gresource.xml
pk-resources.h: packagekit.gresource.xml
	$(AM_V_GEN)					\
	glib-compile-resources				\
		--sourcedir=$(srcdir)			\
		--sourcedir=$(top_builddir)/data	\
		--target=$@				\
		--generate-header			\
		--c-name pk				\
		$(srcdir)/packagekit.gresource.xml

pluginincludedir=$(includedir)/PackageKit/plugin
plugininclude_HEADERS =					\
	packagekit-plugin.h				\
	pk-shared.h					\
	pk-backend.h					\
	pk-backend-spawn.h				\
	pk-backend-job.h				\
	pk-plugin.h					\
	pk-transaction.h

plugininclude_GIRSOURCES =				\
	pk-plugin.h					\
	pk-backend.c					\
	pk-backend.h					\
	pk-backend-job.c				\
	pk-backend-job.h				\
	pk-transaction.c				\
	pk-transaction.h				\
	pk-resources.c					\
	pk-resources.h					\
	pk-shared.c					\
	pk-shared.h

shared_SOURCES =					\
	pk-cleanup.h					\
	pk-dbus.c					\
	pk-dbus.h					\
	pk-transaction.c				\
	pk-transaction.h				\
	pk-transaction-private.h			\
	pk-backend.c					\
	pk-backend.h					\
	pk-backend-job.c				\
	pk-backend-job.h				\
	pk-network.c					\
	pk-network.h					\
	pk-shared.c					\
	pk-shared.h					\
	pk-time.h					\
	pk-time.c					\
	pk-network-stack.h				\
	pk-network-stack.c				\
	pk-network-stack-unix.c				\
	pk-network-stack-unix.h				\
	pk-network-stack-nm.h				\
	pk-network-stack-connman.h			\
	pk-notify.c					\
	pk-notify.h					\
	pk-resources.c					\
	pk-resources.h					\
	pk-spawn.c					\
	pk-spawn.h					\
	pk-sysdep.h					\
	pk-sysdep.c					\
	pk-engine.h					\
	pk-engine.c					\
	pk-backend-spawn.h				\
	pk-backend-spawn.c				\
	pk-transaction-db.h				\
	pk-transaction-db.c				\
	pk-transaction-list.c				\
	pk-transaction-list.h

if PK_BUILD_NETWORKMANAGER
shared_SOURCES +=					\
	pk-network-stack-nm.c
endif

if PK_BUILD_CONNMAN
shared_SOURCES +=					\
	pk-network-stack-connman.c
endif

libexec_PROGRAMS = packagekitd packagekit-direct

packagekit_direct_SOURCES =				\
	pk-backend.c					\
	pk-backend.h					\
	pk-backend-job.c				\
	pk-backend-job.h				\
	pk-cleanup.h					\
	pk-direct.c					\
	pk-shared.c					\
	pk-shared.h					\
	pk-time.c					\
	pk-time.h

packagekit_direct_CPPFLAGS =				\
	$(AM_CPPFLAGS)					\
	-DPK_DB_DIR=\""$(localstatedir)/lib/PackageKit"\"

packagekit_direct_LDADD =				\
	$(GLIB_LIBS)					\
	$(GMODULE_LIBS)					\
	$(NETWORK_MANAGER_LIBS)				\
	$(CONNMAN_LIBS)					\
	$(SQLITE_LIBS)					\
	$(PK_GLIB2_LIBS)				\
	$(POLKIT_LIBS)					\
	$(GIO_LIBS)					\
	$(ARCHIVE_LIBS)					\
	$(SYSTEMD_LIBS)

packagekit_direct_LDFLAGS =				\
	$(PIE_LDFLAGS)					\
	$(RELRO_LDFLAGS)

packagekit_direct_CFLAGS =				\
	$(PIE_CFLAGS)					\
	$(WARNINGFLAGS_C)

packagekitd_SOURCES =					\
	pk-main.c					\
	$(shared_SOURCES)

packagekitd_CPPFLAGS =					\
	$(AM_CPPFLAGS)					\
	-DPK_BUILD_DAEMON=1				\
	-DPK_DB_DIR=\""$(localstatedir)/lib/PackageKit"\"

packagekitd_LDADD =					\
	$(GLIB_LIBS)					\
	$(GMODULE_LIBS)					\
	$(NETWORK_MANAGER_LIBS)				\
	$(CONNMAN_LIBS)					\
	$(SQLITE_LIBS)					\
	$(PK_GLIB2_LIBS)				\
	$(PYTHON_LIBS)					\
	$(POLKIT_LIBS)					\
	$(GIO_LIBS)					\
	$(ARCHIVE_LIBS)					\
	$(SYSTEMD_LIBS)

packagekitd_LDFLAGS =					\
	$(PIE_LDFLAGS)					\
	$(RELRO_LDFLAGS)

packagekitd_CFLAGS =					\
	$(PIE_CFLAGS)					\
	$(WARNINGFLAGS_C)

check_PROGRAMS =					\
	pk-self-test

pk_self_test_SOURCES =					\
	pk-self-test.c					\
	$(shared_SOURCES)

pk_self_test_LDADD =					\
	$(GLIB_LIBS)					\
	$(GMODULE_LIBS)					\
	$(SQLITE_LIBS)					\
	$(NETWORK_MANAGER_LIBS)				\
	$(CONNMAN_LIBS)					\
	$(PK_GLIB2_LIBS)				\
	$(POLKIT_LIBS)					\
	$(GIO_LIBS)					\
	$(ARCHIVE_LIBS)					\
	$(SYSTEMD_LIBS)

pk_self_test_CPPFLAGS =					\
	$(AM_CPPFLAGS)					\
	-DPK_BUILD_DAEMON=1				\
	-DPK_DB_DIR=\""."\"

pk_self_test_CFLAGS =					\
	$(AM_CFLAGS)					\
	$(WARNINGFLAGS_C)

TESTS = pk-self-test

install-data-hook:
	if test -w $(DESTDIR)$(prefix)/; then \
		mkdir -p $(DESTDIR)$(localstatedir)/cache/PackageKit/downloads; \
		mkdir -p $(DESTDIR)$(localstatedir)/cache/PackageKit/metadata; \
		mkdir -p $(DESTDIR)$(localstatedir)/log; \
	fi

clean-local:
	rm -f *~

BUILT_SOURCES =						\
	pk-resources.c					\
	pk-resources.h

CLEANFILES =						\
	$(BUILT_SOURCES)				\
	transactions.db

EXTRA_DIST =						\
	packagekit.gresource.xml

if HAVE_INTROSPECTION

# Build dummy library for g-ir-scanner to parse
noinst_LTLIBRARIES = libpkplugins.la

libpkplugins_la_SOURCES = $(plugininclude_GIRSOURCES) $(shared_SOURCES)

libpkplugins_la_LIBADD =				\
	$(GLIB_LIBS)					\
	$(GMODULE_LIBS)					\
	$(NETWORK_MANAGER_LIBS)				\
	$(CONNMAN_LIBS)					\
	$(SQLITE_LIBS)					\
	$(PK_GLIB2_LIBS)				\
	$(PYTHON_LIBS)					\
	$(POLKIT_LIBS)					\
	$(GIO_LIBS)					\
	$(ARCHIVE_LIBS)					\
	$(SYSTEMD_LIBS)


libpkplugins_la_CPPFLAGS =				\
	$(AM_CPPFLAGS)					\
	-DPK_DB_DIR=\""$(localstatedir)/lib/PackageKit"\"

libpkplugins_la_CFLAGS =				\
	$(PIE_CFLAGS)					\
	$(WARNINGFLAGS_C)

PackageKitPlugin-1.0.gir: libpkplugins.la $(plugininclude_GIRSOURCES)
PackageKitPlugin_1_0_gir_INCLUDES = GObject-2.0 Gio-2.0
PackageKitPlugin_1_0_gir_CFLAGS = $(AM_CPPFLAGS)						\
				-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
				-DI_KNOW_THE_PACKAGEKIT_PLUGIN_API_IS_SUBJECT_TO_CHANGE
PackageKitPlugin_1_0_gir_SCANNERFLAGS = --identifier-prefix=Pk \
					--symbol-prefix=pk \
					--warn-all \
					--include-uninstalled=$(top_builddir)/lib/packagekit-glib2/PackageKitGlib-1.0.gir \
					--add-include-path=$(srcdir) \
					--add-include-path=$(top_builddir)/lib/packagekit-glib2 \
					--library-path=$(top_builddir)/lib/packagekit-glib2 \
					--c-include="plugin/packagekit-plugin.h"
PackageKitPlugin_1_0_gir_LIBS = libpkplugins.la
PackageKitPlugin_1_0_gir_FILES = $(plugininclude_GIRSOURCES)
INTROSPECTION_GIRS += PackageKitPlugin-1.0.gir

girdir = $(datadir)/gir-1.0
gir_DATA = $(INTROSPECTION_GIRS)

typelibdir = $(libdir)/girepository-1.0
typelib_DATA = $(INTROSPECTION_GIRS:.gir=.typelib)

CLEANFILES += $(gir_DATA) $(typelib_DATA)
endif

-include $(top_srcdir)/git.mk
