## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

SUBDIRS = plugins

NULL =

PK_GLIB2_LIBS =						\
	$(top_builddir)/lib/packagekit-glib2/libpackagekit-glib2.la	\
	$(NULL)

INCLUDES =						\
	$(GIO_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	$(POLKIT_CFLAGS)				\
	$(PYTHON_CFLAGS)				\
	$(NETWORK_MANAGER_CFLAGS)			\
	$(CONNMAN_CFLAGS)				\
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
	-DPK_DB_DIR=\""$(PK_DB_DIR)"\"			\
	-DTESTDATADIR=\""$(top_srcdir)/data/tests"\"	\
	-DPK_COMPILATION				\
	-DPOLKIT_LOCAL_I_KNOW_API_IS_SUBJECT_TO_CHANGE	\
	-DG_LOG_DOMAIN=\"PackageKit\"			\
	-I$(top_srcdir)/lib				\
	$(NULL)

pkbackendincludedir=$(includedir)/PackageKit/backend
pkbackendinclude_HEADERS =				\
	pk-backend.h					\
	pk-backend-spawn.h

pluginincludedir=$(includedir)/PackageKit/plugin
plugininclude_HEADERS =					\
	packagekit-plugin.h				\
	pk-backend.h					\
	pk-conf.h					\
	pk-plugin.h					\
	pk-transaction.h

shared_SOURCES =					\
	pk-marshal.c					\
	pk-marshal.h					\
	pk-dbus.c					\
	pk-dbus.h					\
	pk-transaction.c				\
	pk-transaction.h				\
	pk-transaction-dbus.h				\
	pk-backend.c					\
	pk-backend.h					\
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
	pk-syslog.h					\
	pk-syslog.c					\
	pk-conf.c					\
	pk-conf.h					\
	pk-store.c					\
	pk-store.h					\
	pk-cache.c					\
	pk-cache.h					\
	pk-notify.c					\
	pk-notify.h					\
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
	pk-transaction-list.h				\
	$(NULL)

if PK_BUILD_NETWORKMANAGER
shared_SOURCES +=					\
	pk-network-stack-nm.c
endif

if PK_BUILD_CONNMAN
shared_SOURCES +=					\
	pk-network-stack-connman.c
endif

libexec_PROGRAMS =					\
	packagekitd					\
	$(NULL)

packagekitd_SOURCES =					\
	pk-main.c					\
	$(shared_SOURCES)				\
	$(NULL)

packagekitd_LDADD =					\
	$(GLIB_LIBS)					\
	$(GMODULE_LIBS)					\
	$(NETWORK_MANAGER_LIBS)				\
	$(CONNMAN_LIBS)					\
	$(DBUS_LIBS)					\
	$(SQLITE_LIBS)					\
	$(PK_GLIB2_LIBS)				\
	$(PYTHON_LIBS)					\
	$(POLKIT_LIBS)					\
	$(GIO_LIBS)					\
	$(ARCHIVE_LIBS)					\
	$(NULL)

packagekitd_CFLAGS =					\
	$(WARNINGFLAGS_C)				\
	$(NULL)

if BACKEND_TYPE_BOX
packagekitd_INCLUDES =					\
	$(BOX_CFLAGS)
packagekitd_LDADD +=					\
	$(BOX_LIBS)
endif

BUILT_SOURCES =						\
	pk-marshal.c					\
	pk-marshal.h					\
	org.freedesktop.PackageKit.h			\
	org.freedesktop.PackageKit.Transaction.h

pk-marshal.c: pk-marshal.list
	echo "#include \"pk-marshal.h\"" > $@ && \
	glib-genmarshal $< --prefix=pk_marshal --body >> $@

pk-marshal.h: pk-marshal.list
	glib-genmarshal $< --prefix=pk_marshal --header > $@

org.freedesktop.PackageKit.h: org.freedesktop.PackageKit.xml
	$(LIBTOOL) --mode=execute dbus-binding-tool	\
		--prefix=pk_engine			\
		--mode=glib-server			\
		--output=org.freedesktop.PackageKit.h	\
		$(srcdir)/org.freedesktop.PackageKit.xml

org.freedesktop.PackageKit.Transaction.h: org.freedesktop.PackageKit.Transaction.xml
	$(LIBTOOL) --mode=execute dbus-binding-tool	\
		--prefix=pk_transaction			\
		--mode=glib-server			\
		--output=org.freedesktop.PackageKit.Transaction.h	\
		$(srcdir)/org.freedesktop.PackageKit.Transaction.xml

if EGG_BUILD_TESTS

check_PROGRAMS =					\
	pk-self-test

pk_self_test_SOURCES =					\
	pk-self-test.c					\
	$(shared_SOURCES)				\
	$(NULL)

pk_self_test_LDADD =					\
	$(GLIB_LIBS)					\
	$(GMODULE_LIBS)					\
	$(DBUS_LIBS)					\
	$(SQLITE_LIBS)					\
	$(NETWORK_MANAGER_LIBS)				\
	$(CONNMAN_LIBS)					\
	$(PK_GLIB2_LIBS)				\
	$(POLKIT_LIBS)					\
	$(GIO_LIBS)					\
	$(ARCHIVE_LIBS)					\
	$(NULL)

pk_self_test_CFLAGS = -DEGG_TEST $(AM_CFLAGS)

TESTS = pk-self-test
endif

install-data-hook:
	if test -w $(DESTDIR)$(prefix)/; then \
		mkdir -p $(DESTDIR)$(localstatedir)/cache/PackageKit/downloads; \
		mkdir -p $(DESTDIR)$(PK_LOG_DIR); \
	fi

EXTRA_DIST =						\
	pk-marshal.list					\
	org.freedesktop.PackageKit.xml			\
	org.freedesktop.PackageKit.Transaction.xml	\
	$(NULL)

clean-local:
	rm -f *~
	rm -f transactions.db
	rm -f pk-marshal.c pk-marshal.h

CLEANFILES = *~ $(BUILT_SOURCES)

-include $(top_srcdir)/git.mk
