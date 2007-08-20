INCLUDES = \
	$(GLIB_CFLAGS)						\
	$(DBUS_CFLAGS)						\
	-I$(top_srcdir)/libgbus-glib				\
	-DPACKAGE_DATA_DIR=\""$(datadir)"\"			\
	-DPACKAGE_LOCALE_DIR=\""$(prefix)/$(DATADIRNAME)/locale"\"

noinst_LTLIBRARIES =						\
	libgbus.la

libgbus_la_SOURCES =						\
	libgbus-marshal.h					\
	libgbus-marshal.c					\
	libgbus.c						\
	libgbus.h
libgbus_la_LIBADD = @DBUS_LIBS@ $(INTLLIBS) $(GLIB_LIBS)

EXTRA_DIST =							\
	libgbus-marshal.list

BUILT_SOURCES = 						\
	libgbus-marshal.c					\
	libgbus-marshal.h

libgbus-marshal.c: libgbus-marshal.list
	echo "#include \"libgbus-marshal.h\"" > $@ && \
	@GLIB_GENMARSHAL@ $< --prefix=libgbus_marshal --body >> $@

libgbus-marshal.h: libgbus-marshal.list
	@GLIB_GENMARSHAL@ $< --prefix=libgbus_marshal --header > $@

clean-local:
	rm -f *~
	rm -f libgbus-marshal.c libgbus-marshal.h

CLEANFILES = $(BUILT_SOURCES)

