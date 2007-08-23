## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

NULL =

autostartdir = $(datadir)/gnome/autostart
autostart_DATA = pk-update-icon.desktop

desktopdir = $(datadir)/applications
desktop_DATA = pk-application.desktop

pkgdata_DATA =						\
	pk-application.glade

EXTRA_DIST =						\
	pk-update-icon.desktop				\
	pk-application.desktop				\
	$(pkgdata_DATA)					\
	$(NULL)

INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(LIBGLADE_CFLAGS)				\
	$(LIBNOTIFY_CFLAGS)				\
	$(GTK_CFLAGS)					\
	-DBINDIR=\"$(bindir)\"			 	\
	-DDATADIR=\"$(datadir)\"			\
	-DPREFIX=\""$(prefix)"\" 			\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DLIBDIR=\""$(libdir)"\" 			\
	-DVERSION="\"$(VERSION)\"" 			\
	-DPK_DATA=\"$(pkgdatadir)\"			\
	-I$(top_srcdir)/libpackagekit			\
	$(NULL)

PK_LIBS =						\
	$(top_builddir)/libpackagekit/libpackagekit.la	\
	$(NULL)

bin_PROGRAMS =						\
	pkcon						\
	pkmon						\
	pk-application					\
	pk-update-icon					\
	$(NULL)

pkcon_SOURCES =						\
	pk-console.c					\
	$(NULL)

pkcon_LDADD =						\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(PK_LIBS)					\
	$(NULL)

pkmon_SOURCES =						\
	pk-monitor.c					\
	$(NULL)

pkmon_LDADD =						\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(PK_LIBS)					\
	$(NULL)

pk_update_icon_SOURCES =				\
	pk-update-icon.c				\
	pk-notify.c					\
	pk-notify.h					\
	$(NULL)

pk_update_icon_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBNOTIFY_LIBS)				\
	$(GTK_LIBS)					\
	$(PK_LIBS)					\
	$(NULL)

pk_application_SOURCES =				\
	pk-application-main.c				\
	pk-application.c				\
	pk-application.h				\
	$(NULL)

pk_application_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GTK_LIBS)					\
	$(PK_LIBS)					\
	$(NULL)

clean-local:
	rm -f *~

CLEANFILES = $(BUILT_SOURCES)

DISTCLEANFILES =					\
	$(NULL)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

