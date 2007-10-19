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
	$(GLIB_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	$(POLKIT_CFLAGS)				\
	-DBINDIR=\"$(bindir)\"			 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DVERSION="\"$(VERSION)\"" 			\
	-DPK_DATA=\"$(pkgdatadir)\"			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\" 		\
	-DDATABASEDIR=\""$(PK_DB_DIR)"\"		\
	-I$(top_srcdir)/libpackagekit			\
	-I$(top_srcdir)/libselftest			\
	$(NULL)

pkbackendincludedir=$(includedir)/packagekit-backend

pkbackendinclude_HEADERS =				\
	pk-backend.h

sbin_PROGRAMS =						\
	packagekitd					\
	$(NULL)

packagekitd_SOURCES =					\
	pk-main.c					\
	pk-marshal.c					\
	pk-marshal.h					\
	pk-backend-internal.h				\
	pk-backend.c					\
	pk-backend.h					\
	pk-security.h					\
	pk-conf.c					\
	pk-conf.h					\
	pk-spawn.c					\
	pk-spawn.h					\
	pk-engine.h					\
	pk-engine.c					\
	pk-inhibit.h					\
	pk-inhibit.c					\
	pk-thread-list.h				\
	pk-thread-list.c				\
	pk-transaction-db.h				\
	pk-transaction-db.c				\
	pk-transaction-id.h				\
	pk-transaction-id.c				\
	pk-transaction-list.c				\
	pk-transaction-list.h				\
	$(NULL)

if SECURITY_TYPE_POLKIT
packagekitd_SOURCES += pk-security-polkit.c
endif

if SECURITY_TYPE_DUMMY
packagekitd_SOURCES += pk-security-dummy.c
endif

packagekitd_LDADD =					\
	$(GLIB_LIBS)					\
	$(GMODULE_LIBS)					\
	$(DBUS_LIBS)					\
	$(SQLITE_LIBS)					\
	$(PK_LIBS)					\
	$(POLKIT_LIBS)					\
	$(NULL)

if BACKEND_TYPE_BOX
packagekitd_INCLUDES =	 				\
	$(BOX_CFLAGS)
packagekitd_LDADD += 					\
	$(BOX_LIBS)
endif

if BACKEND_TYPE_APT
INCLUDES +=						\
	$(APT_CFLAGS)
packagekitd_LDADD += 					\
	$(APT_LIBS)
endif

BUILT_SOURCES = 					\
	pk-marshal.c					\
	pk-marshal.h					\
	pk-interface.h					\
	$(NULL)

pk-marshal.c: pk-marshal.list
	echo "#include \"pk-marshal.h\"" > $@ && \
	glib-genmarshal $< --prefix=pk_marshal --body >> $@

pk-marshal.h: pk-marshal.list
	glib-genmarshal $< --prefix=pk_marshal --header > $@

pk-interface.h: pk-interface.xml
	libtool --mode=execute dbus-binding-tool	\
		--prefix=pk_engine			\
		--mode=glib-server			\
		--output=pk-interface.h	\
		$(srcdir)/pk-interface.xml

check_PROGRAMS =					\
	pk-self-test

noinst_PROGRAMS =					\
	pk-self-test

pk_self_test_SOURCES =					\
	pk-inhibit.h					\
	pk-inhibit.c					\
	pk-backend.h					\
	pk-backend.c					\
	pk-spawn.h					\
	pk-spawn.c					\
	pk-conf.h					\
	pk-conf.c					\
	pk-thread-list.h				\
	pk-thread-list.c				\
	pk-transaction-id.h				\
	pk-transaction-id.c				\
	pk-transaction-list.h				\
	pk-transaction-list.c				\
	pk-self-test.c					\
	$(NULL)

pk_self_test_LDADD =					\
	$(GLIB_LIBS)					\
	$(GMODULE_LIBS)					\
	$(DBUS_LIBS)					\
	$(SELFTEST_LIBS)				\
	$(LIBNM_LIBS)					\
	$(PK_LIBS)					\
	$(POLKIT_LIBS)					\
	$(NULL)

pk_self_test_CPPFLAGS=	\
	-DPK_BUILD_TESTS

EXTRA_DIST =						\
	pk-marshal.list					\
	pk-security-polkit.c				\
	pk-security-dummy.c				\
	pk-interface.xml				\
	$(NULL)

clean-local:
	rm -f *~
	rm -f pk-marshal.c pk-marshal.h

CLEANFILES = $(BUILT_SOURCES)

TESTS = pk-self-test

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

