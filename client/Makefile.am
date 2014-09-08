## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

EXTRA_DIST =						\
	pkcon.xml					\
	pkmon.xml

if HAVE_XSLTPROC
man_MANS =						\
	pkcon.1						\
	pkmon.1
endif

if HAVE_XSLTPROC
pkcon.1: pkcon.xml
	xsltproc http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $? &> /dev/null

pkmon.1: pkmon.xml
	xsltproc http://docbook.sourceforge.net/release/xsl/current/manpages/docbook.xsl $? &> /dev/null
endif

AM_CPPFLAGS =						\
	$(PIE_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	$(GIO_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	-DBINDIR=\"$(bindir)\"			 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DLIBEXECDIR=\""$(libexecdir)"\" 		\
	-DVERSION="\"$(VERSION)\"" 			\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"		\
	-DPK_DATA=\"$(pkgdatadir)\"			\
	-DPK_DB_DIR=\""$(PK_DB_DIR)"\" 			\
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
	-DPK_COMPILATION				\
	-DG_LOG_DOMAIN=\"PackageKit\"			\
	-I$(top_builddir)/lib				\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/lib

bin_PROGRAMS =						\
	pkcon						\
	pkmon

PK_GLIB2_LIBS =						\
	$(top_builddir)/lib/packagekit-glib2/libpackagekit-glib2.la	\
	$(top_builddir)/lib/packagekit-glib2/libpackagekitprivate.a

pkcon_SOURCES =						\
	pk-console.c

pkcon_LDADD =						\
	$(GLIB_LIBS)					\
	$(GIO_LIBS)					\
	$(DBUS_LIBS)					\
	$(SQLITE_LIBS)					\
	$(PK_GLIB2_LIBS)

pkcon_LDFLAGS =						\
	$(PIE_LDFLAGS)

pkcon_CFLAGS =						\
	$(WARNINGFLAGS_C)

pkmon_SOURCES =						\
	pk-monitor.c

pkmon_LDADD =						\
	$(GLIB_LIBS)					\
	$(GIO_LIBS)					\
	$(DBUS_LIBS)					\
	$(SQLITE_LIBS)					\
	$(PK_GLIB2_LIBS)

pkmon_LDFLAGS =						\
	$(PIE_LDFLAGS)

pkmon_CFLAGS =						\
	$(WARNINGFLAGS_C)

if PK_BUILD_SYSTEMD_UPDATES

libexec_PROGRAMS =					\
	pk-offline-update

pk_offline_update_SOURCES =				\
	pk-offline-update.c

pk_offline_update_LDADD =				\
	$(GLIB_LIBS)					\
	$(GIO_LIBS)					\
	$(LIBSYSTEMD_JOURNAL_LIBS)			\
	$(PK_GLIB2_LIBS)

pk_offline_update_LDFLAGS =				\
	$(PIE_LDFLAGS)

pk_offline_update_CFLAGS =				\
	$(WARNINGFLAGS_C)
endif

clean-local:
	rm -f *~
	rm -f *.1
	rm -f manpage.*
	rm -f *.out

CLEANFILES = $(BUILT_SOURCES) $(man_MANS)

MAINTAINERCLEANFILES =					\
	*~						\
	Makefile.in


-include $(top_srcdir)/git.mk
