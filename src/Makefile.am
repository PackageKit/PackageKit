## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

PK_LIBS =						\
	$(top_builddir)/libpackagekit/libpackagekit.la	\
	$(NULL)

SELFTEST_LIBS =						\
	$(top_builddir)/libselftest/libselftest.la	\
	$(NULL)

INCLUDES =						\
	$(GIO_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	$(POLKIT_CFLAGS)				\
	-DBINDIR=\"$(bindir)\"			 	\
	-DSBINDIR=\"$(sbindir)\"		 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DVERSION="\"$(VERSION)\"" 			\
	-DPK_DATA=\"$(pkgdatadir)\"			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\" 		\
	-DPK_DB_DIR=\""$(PK_DB_DIR)"\" 			\
	-I$(top_srcdir)/libpackagekit			\
	-I$(top_srcdir)/libselftest			\
	-I$(top_srcdir)/libgbus				\
	$(NULL)

pkbackendincludedir=$(includedir)/packagekit-backend

pkbackendinclude_HEADERS =				\
	pk-backend.h					\
	pk-backend-spawn.h				\
	pk-backend-thread.h				\
	$(NULL)

shared_SOURCES =					\
	pk-marshal.c					\
	pk-marshal.h					\
	pk-runner.c					\
	pk-runner.h					\
	pk-backend.c					\
	pk-backend.h					\
	pk-backend-internal.h				\
	pk-security.h					\
	pk-time.h					\
	pk-time.c					\
	pk-conf.c					\
	pk-conf.h					\
	pk-cache.c					\
	pk-cache.h					\
	pk-notify.c					\
	pk-notify.h					\
	pk-spawn.c					\
	pk-spawn.h					\
	pk-restart.h					\
	pk-engine.h					\
	pk-engine.c					\
	pk-inhibit.h					\
	pk-inhibit.c					\
	pk-thread-list.h				\
	pk-thread-list.c				\
	pk-backend-spawn.h				\
	pk-backend-spawn.c				\
	pk-backend-thread.h				\
	pk-backend-thread.c				\
	pk-backend-dbus.h				\
	pk-backend-dbus.c				\
	pk-transaction-db.h				\
	pk-transaction-db.c				\
	pk-transaction-id.h				\
	pk-transaction-id.c				\
	pk-transaction-list.c				\
	pk-transaction-list.h				\
	$(NULL)

if PK_BUILD_GIO
shared_SOURCES +=					\
	pk-restart-gio.c				\
	$(NULL)
else
shared_SOURCES +=					\
	pk-restart-dummy.c				\
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
	$(DBUS_LIBS)					\
	$(SQLITE_LIBS)					\
	$(PK_LIBS)					\
	$(POLKIT_LIBS)					\
	$(SELFTEST_LIBS)				\
	$(GIO_LIBS)					\
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

if PK_BUILD_TESTS

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
	$(SELFTEST_LIBS)				\
	$(SQLITE_LIBS)					\
	$(LIBNM_LIBS)					\
	$(PK_LIBS)					\
	$(POLKIT_LIBS)					\
	$(GIO_LIBS)					\
	$(NULL)

TESTS = pk-self-test
endif

if PK_BUILD_GCOV
clean-gcov:
	rm -f *.gcov *.gcda
gcov: clean-gcov all check
	$(top_srcdir)/tools/create-coverage-report.sh packagekit $(filter %.c,$(packagekitd_SOURCES)) > gcov.txt
endif

if PK_BUILD_GPROF
clean-gprof:
	rm -f *.out
gprof: clean-gprof all check
	gprof .libs/pk-self-test > gprof.txt
endif

EXTRA_DIST =						\
	pk-marshal.list					\
	pk-security-polkit.c				\
	pk-security-dummy.c				\
	pk-interface.xml				\
	pk-interface-transaction.xml			\
	$(NULL)

clean-local:
	rm -f *~
	rm -f *.out
	rm -f *.gcda
	rm -f *.gcno
	rm -f gcov.txt
	rm -f gprof.txt
	rm -f pk-marshal.c pk-marshal.h

CLEANFILES = *~ $(BUILT_SOURCES)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

