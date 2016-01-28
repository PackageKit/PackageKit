## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

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

shared_SOURCES =					\
	pk-dbus.c					\
	pk-dbus.h					\
	pk-transaction.c				\
	pk-transaction.h				\
	pk-transaction-private.h			\
	pk-backend.c					\
	pk-backend.h					\
	pk-backend-job.c				\
	pk-backend-job.h				\
	pk-shared.c					\
	pk-shared.h					\
	pk-resources.c					\
	pk-resources.h					\
	pk-spawn.c					\
	pk-spawn.h					\
	pk-engine.h					\
	pk-engine.c					\
	pk-backend-spawn.h				\
	pk-backend-spawn.c				\
	pk-scheduler.c					\
	pk-scheduler.h					\
	pk-transaction-db.c				\
	pk-transaction-db.h

libexec_PROGRAMS = packagekitd packagekit-direct

packagekit_direct_SOURCES =				\
	pk-backend.c					\
	pk-backend.h					\
	pk-backend-job.c				\
	pk-backend-job.h				\
	pk-direct.c					\
	pk-shared.c					\
	pk-shared.h

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

-include $(top_srcdir)/git.mk
