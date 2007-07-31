## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

EXTRA_DIST =						\
	pk-marshal.list				\
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
	pk-engine.h					\
	pk-engine.c					\
	$(NULL)

BUILT_SOURCES = 					\
	pk-marshal.c					\
	pk-marshal.h					\
	$(NULL)

pk-marshal.c: pk-marshal.list
	echo "#include \"pk-marshal.h\"" > $@ && \
	@GLIB_GENMARSHAL@ $< --prefix=gpm_marshal --body >> $@

pk-marshal.h: pk-marshal.list
	@GLIB_GENMARSHAL@ $< --prefix=gpm_marshal --header > $@

clean-local:
	rm -f *~
	rm -f pk-marshal.c pk-marshal.h

CLEANFILES = $(BUILT_SOURCES)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

