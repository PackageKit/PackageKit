## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

EXTRA_DIST =						\
	pk-marshal.list					\
	pk-interface.xml				\
	$(NULL)

INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	-DBINDIR=\"$(bindir)\"			 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DVERSION="\"$(VERSION)\"" 			\
	-DPK_DATA=\"$(pkgdatadir)\"			\
	$(NULL)

sbin_PROGRAMS =						\
	packagekitd					\
	$(NULL)

packagekitd_SOURCES =					\
	pk-main.c					\
	pk-task.h					\
	pk-task.c					\
	pk-task-common.h				\
	pk-task-common.c				\
	pk-marshal.c					\
	pk-marshal.h					\
	pk-debug.c					\
	pk-debug.h					\
	pk-engine.h					\
	pk-engine.c					\
	$(NULL)

packagekitd_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(NULL)

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

clean-local:
	rm -f *~
	rm -f pk-marshal.c pk-marshal.h

CLEANFILES = $(BUILT_SOURCES)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

