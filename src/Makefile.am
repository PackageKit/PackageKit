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
	$(POLKIT_CFLAGS)				\
	$(NM_CFLAGS)					\
	-DBINDIR=\"$(bindir)\"			 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DVERSION="\"$(VERSION)\"" 			\
	-DPK_DATA=\"$(pkgdatadir)\"			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\" 		\
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
	pk-conf.c					\
	pk-conf.h					\
	pk-job-list.c					\
	pk-job-list.h					\
	pk-spawn.c					\
	pk-spawn.h					\
	pk-engine.h					\
	pk-engine.c					\
	pk-network.h					\
	pk-network.c					\
	$(NULL)

packagekitd_LDADD =					\
	$(GLIB_LIBS)					\
	$(GMODULE_LIBS)					\
	$(DBUS_LIBS)					\
	$(PK_LIBS)					\
	$(LIBNM_LIBS)					\
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

pk_self_test_SOURCES =					\
	pk-spawn.h					\
	pk-spawn.c					\
	pk-self-test.c					\
	$(NULL)

pk_self_test_LDADD =					\
	$(GLIB_LIBS)					\
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
	pk-interface.xml				\
	pk-spawn-test.sh				\
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

