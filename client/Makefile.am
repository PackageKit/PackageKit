## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

EXTRA_DIST =						\
	pkcon.xml					\
	pkmon.xml					\
	pkgenpack.xml					\
	$(NULL)

if HAVE_XSLTPROC
man_MANS =						\
	pkcon.1						\
	pkmon.1						\
	pkgenpack.1					\
	$(NULL)
endif

if HAVE_XSLTPROC
pkcon.1: pkcon.xml
	xsltproc http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $? &> /dev/null

pkmon.1: pkmon.xml
	xsltproc http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $? &> /dev/null

pkgenpack.1: pkgenpack.xml
	xsltproc http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $? &> /dev/null
endif

INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	-DBINDIR=\"$(bindir)\"			 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DVERSION="\"$(VERSION)\"" 			\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"		\
	-DPK_DATA=\"$(pkgdatadir)\"			\
	-DPK_DB_DIR=\""$(PK_DB_DIR)"\" 			\
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
	-I$(top_srcdir)/lib				\
	$(NULL)

bin_PROGRAMS =						\
	pkcon						\
	pkmon						\
	pkgenpack					\
	$(NULL)

PK_GLIB2_LIBS =						\
	$(top_builddir)/lib/packagekit-glib2/libpackagekit-glib2.la	\
	$(top_builddir)/lib/packagekit-glib2/libpackagekitprivate.a	\
	$(NULL)

pkcon_SOURCES =						\
	pk-console.c					\
	$(NULL)

pkcon_LDADD =						\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(SQLITE_LIBS)					\
	$(PK_GLIB2_LIBS)				\
	$(NULL)

pkcon_CFLAGS =						\
	$(WARNINGFLAGS_C)				\
	$(NULL)

pkmon_SOURCES =						\
	pk-monitor.c					\
	$(NULL)

pkmon_LDADD =						\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(SQLITE_LIBS)					\
	$(PK_GLIB2_LIBS)				\
	$(NULL)

pkmon_CFLAGS =						\
	$(WARNINGFLAGS_C)				\
	$(NULL)

pkgenpack_SOURCES =					\
	pk-generate-pack.c				\
	$(NULL)

pkgenpack_LDADD = 					\
	$(ARCHIVE_LIBS)					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(SQLITE_LIBS)					\
	$(PK_GLIB2_LIBS)				\
	$(NULL)

pkgenpack_CFLAGS =					\
	$(WARNINGFLAGS_C)				\
	$(NULL)

clean-local:
	rm -f *~
	rm -f *.1
	rm -f manpage.*
	rm -f *.out

CLEANFILES = $(BUILT_SOURCES)

DISTCLEANFILES =					\
	$(NULL)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

