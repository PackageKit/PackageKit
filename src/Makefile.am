## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

PK_LIBS =						\
	$(top_builddir)/libpackagekit/libpackagekit.la	\
	$(NULL)

INCLUDES =						\
	$(GIO_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	$(POLKIT_CFLAGS)				\
	$(PYTHON_CFLAGS)				\
	$(LIBNM_CFLAGS)					\
	-DBINDIR=\"$(bindir)\"			 	\
	-DSBINDIR=\"$(sbindir)\"		 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DLIBEXECDIR=\""$(libexecdir)"\" 		\
	-DVERSION="\"$(VERSION)\"" 			\
	-DPK_DATA=\"$(pkgdatadir)\"			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\" 		\
	-DPK_DB_DIR=\""$(PK_DB_DIR)"\" 			\
	-DEGG_LOG_FILE=\""$(PK_LOG_DIR)/PackageKit"\"	\
	-DEGG_VERBOSE="\"PK_VERBOSE\""			\
	-DEGG_LOGGING="\"PK_LOGGING\""			\
	-DEGG_CONSOLE="\"PK_CONSOLE\""			\
	-I$(top_srcdir)/libpackagekit			\
	$(NULL)

pkbackendincludedir=$(includedir)/packagekit-backend

pkbackendinclude_HEADERS =				\
	pk-backend.h					\
	pk-backend-spawn.h				\
	$(NULL)

shared_SOURCES =					\
	egg-debug.c					\
	egg-debug.h					\
	egg-string.c					\
	egg-string.h					\
	egg-obj-list.c					\
	egg-obj-list.h					\
	egg-string-list.c				\
	egg-string-list.h				\
	egg-dbus-monitor.c				\
	egg-dbus-monitor.h				\
	pk-marshal.c					\
	pk-marshal.h					\
	pk-transaction.c				\
	pk-transaction.h				\
	pk-backend.c					\
	pk-backend.h					\
	pk-backend-internal.h				\
	pk-security.h					\
	pk-network.c					\
	pk-network.h					\
	pk-shared.c					\
	pk-shared.h					\
	pk-post-trans.c					\
	pk-post-trans.h					\
	pk-network-nm.h					\
	pk-network-unix.c				\
	pk-network-unix.h				\
	pk-time.h					\
	pk-time.c					\
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
	pk-update-detail-list.c				\
	pk-update-detail-list.h				\
	pk-file-monitor.h				\
	pk-engine.h					\
	pk-engine.c					\
	pk-inhibit.h					\
	pk-inhibit.c					\
	pk-backend-spawn.h				\
	pk-backend-spawn.c				\
	pk-backend-dbus.h				\
	pk-backend-dbus.c				\
	pk-transaction-db.h				\
	pk-transaction-db.c				\
	pk-transaction-id.h				\
	pk-transaction-id.c				\
	pk-transaction-list.c				\
	pk-transaction-list.h				\
	$(NULL)

if PK_BUILD_NETWORKMANAGER
shared_SOURCES +=					\
	pk-network-nm.c
else
shared_SOURCES +=					\
	pk-network-nm-dummy.c
endif

if PK_BUILD_GIO
shared_SOURCES +=					\
	pk-file-monitor-gio.c				\
	$(NULL)
else
shared_SOURCES +=					\
	pk-file-monitor-dummy.c				\
	$(NULL)
endif

if SECURITY_TYPE_POLKIT
shared_SOURCES += pk-security-polkit.c
endif

if SECURITY_TYPE_DUMMY
shared_SOURCES += pk-security-dummy.c
endif

sbin_PROGRAMS =						\
	packagekitd					\
	$(NULL)

packagekitd_SOURCES =					\
	pk-main.c					\
	$(shared_SOURCES)				\
	$(NULL)

packagekitd_LDADD =					\
	$(GLIB_LIBS)					\
	$(GMODULE_LIBS)					\
	$(LIBNM_LIBS)					\
	$(DBUS_LIBS)					\
	$(SQLITE_LIBS)					\
	$(PK_LIBS)					\
	$(PYTHON_LIBS)					\
	$(POLKIT_LIBS)					\
	$(GIO_LIBS)					\
	$(ARCHIVE_LIBS)					\
	$(NULL)

if BACKEND_TYPE_BOX
packagekitd_INCLUDES =	 				\
	$(BOX_CFLAGS)
packagekitd_LDADD += 					\
	$(BOX_LIBS)
endif

BUILT_SOURCES = 					\
	pk-marshal.c					\
	pk-marshal.h					\
	pk-interface.h					\
	pk-interface-transaction.h			\
	$(NULL)

pk-marshal.c: pk-marshal.list
	echo "#include \"pk-marshal.h\"" > $@ && \
	glib-genmarshal $< --prefix=pk_marshal --body >> $@

pk-marshal.h: pk-marshal.list
	glib-genmarshal $< --prefix=pk_marshal --header > $@

pk-interface.h: pk-interface.xml
	$(LIBTOOL) --mode=execute dbus-binding-tool	\
		--prefix=pk_engine			\
		--mode=glib-server			\
		--output=pk-interface.h			\
		$(srcdir)/pk-interface.xml

pk-interface-transaction.h: pk-interface-transaction.xml
	$(LIBTOOL) --mode=execute dbus-binding-tool	\
		--prefix=pk_transaction			\
		--mode=glib-server			\
		--output=pk-interface-transaction.h	\
		$(srcdir)/pk-interface-transaction.xml

if EGG_BUILD_TESTS

check_PROGRAMS =					\
	pk-self-test

pk_self_test_SOURCES =					\
	pk-self-test.c					\
	egg-test.h					\
	egg-test.c					\
	$(shared_SOURCES)				\
	$(NULL)

pk_self_test_LDADD =					\
	$(GLIB_LIBS)					\
	$(GMODULE_LIBS)					\
	$(DBUS_LIBS)					\
	$(SQLITE_LIBS)					\
	$(LIBNM_LIBS)					\
	$(PK_LIBS)					\
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
	fi

EXTRA_DIST =						\
	pk-marshal.list					\
	pk-security-polkit.c				\
	pk-security-dummy.c				\
	pk-interface.xml				\
	pk-interface-transaction.xml			\
	$(NULL)

clean-local:
	rm -f *~
	rm -f pk-marshal.c pk-marshal.h

CLEANFILES = *~ $(BUILT_SOURCES)

