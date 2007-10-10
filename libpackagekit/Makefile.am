
NULL =

INCLUDES = \
	$(GLIB_CFLAGS)						\
	$(DBUS_CFLAGS)						\
	$(LIBNM_CFLAGS)						\
	-I$(top_srcdir)/libpackagekit				\
	-I$(top_srcdir)/libgbus					\
	-I$(top_srcdir)/libselftest				\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"			\
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\"

GBUS_LIBS =							\
	$(top_builddir)/libgbus/libgbus.la			\
	$(NULL)

SELFTEST_LIBS =							\
	$(top_builddir)/libselftest/libselftest.la		\
	$(NULL)

lib_LTLIBRARIES =						\
	libpackagekit.la					\
	$(NULL)

libpackagekit_includedir = $(includedir)/packagekit

libpackagekit_include_HEADERS =					\
	pk-debug.h						\
	pk-connection.h						\
	pk-network.h						\
	pk-package-id.h						\
	pk-package-list.h					\
	pk-enum-list.h						\
	pk-enum.h						\
	pk-task-common.h					\
	pk-client.h						\
	pk-task-list.h						\
	pk-job-list.h						\
	pk-polkit-client.h					\
	$(NULL)

libpackagekit_la_SOURCES =					\
	pk-debug.c						\
	pk-debug.h						\
	pk-marshal.c						\
	pk-marshal.h						\
	pk-connection.c						\
	pk-connection.h						\
	pk-network.c						\
	pk-network.h						\
	pk-package-id.c						\
	pk-package-id.h						\
	pk-package-list.c					\
	pk-package-list.h					\
	pk-enum-list.c						\
	pk-enum-list.h						\
	pk-enum.h						\
	pk-enum.c						\
	pk-task-common.c					\
	pk-task-common.h					\
	pk-client.c						\
	pk-client.h						\
	pk-task-list.c						\
	pk-task-list.h						\
	pk-job-list.c						\
	pk-job-list.h						\
	pk-polkit-client.c					\
	pk-polkit-client.h					\
	$(NULL)

libpackagekit_la_LIBADD =					\
	$(GLIB_LIBS)						\
	$(DBUS_LIBS)						\
	$(GBUS_LIBS)						\
	$(LIBNM_LIBS)						\
	$(NULL)

libpackagekit_la_LDFLAGS =					\
	-version-info $(LT_CURRENT):$(LT_REVISION):$(LT_AGE)	\
	$(NULL)

check_PROGRAMS =						\
	pk-self-test

pk_self_test_SOURCES =						\
	pk-debug.c						\
	pk-debug.h						\
	pk-enum.h						\
	pk-enum.c						\
	pk-enum-list.h						\
	pk-enum-list.c						\
	pk-package-id.h						\
	pk-package-id.c						\
	pk-package-list.h					\
	pk-package-list.c					\
	pk-self-test.c						\
	$(NULL)

pk_self_test_LDADD =						\
	$(GLIB_LIBS)						\
	$(DBUS_LIBS)						\
	$(SELFTEST_LIBS)					\
	$(PK_LIBS)						\
	$(NULL)

pk_self_test_CPPFLAGS=	\
	-DPK_BUILD_TESTS

EXTRA_DIST =							\
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

clean-local:
	rm -f *~
	rm -f pk-marshal.c pk-marshal.h

CLEANFILES = $(BUILT_SOURCES)

TESTS = pk-self-test

