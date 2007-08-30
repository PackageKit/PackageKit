INCLUDES = \
	$(GLIB_CFLAGS)						\
	$(DBUS_CFLAGS)						\
	-I$(top_srcdir)/libselftest-glib			\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"			\
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\"

noinst_LTLIBRARIES =						\
	libselftest.la

libselftest_la_SOURCES =					\
	libselftest.c						\
	libselftest.h
libselftest_la_LIBADD = @DBUS_LIBS@ $(INTLLIBS) $(GLIB_LIBS)

clean-local:
	rm -f *~

CLEANFILES = $(BUILT_SOURCES)

