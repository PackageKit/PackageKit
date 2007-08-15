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
	pk-marshal.list					\
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
	$(NULL)

bin_PROGRAMS =						\
	pkcon						\
	pkmon						\
	pk-application					\
	pk-update-icon					\
	$(NULL)

pkcon_SOURCES =						\
	pk-console.c					\
	pk-debug.c					\
	pk-debug.h					\
	pk-marshal.c					\
	pk-marshal.h					\
	pk-task-utils.h					\
	pk-task-utils.c					\
	pk-task-common.c				\
	pk-task-common.h				\
	pk-task-client.c				\
	pk-task-client.h				\
	$(NULL)

pkcon_LDADD =						\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(NULL)

pkmon_SOURCES =						\
	pk-monitor.c					\
	pk-debug.c					\
	pk-debug.h					\
	pk-marshal.c					\
	pk-marshal.h					\
	pk-task-list.c					\
	pk-task-list.h					\
	pk-task-client.c				\
	pk-task-client.h				\
	pk-task-common.c				\
	pk-task-common.h				\
	pk-task-utils.h					\
	pk-task-utils.c					\
	pk-job-list.c					\
	pk-job-list.h					\
	$(NULL)

pkmon_LDADD =						\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(NULL)

pk_update_icon_SOURCES =				\
	pk-update-icon.c				\
	pk-debug.c					\
	pk-debug.h					\
	pk-marshal.c					\
	pk-marshal.h					\
	pk-task-list.c					\
	pk-task-list.h					\
	pk-task-client.c				\
	pk-task-client.h				\
	pk-task-common.c				\
	pk-task-common.h				\
	pk-task-utils.h					\
	pk-task-utils.c					\
	pk-job-list.c					\
	pk-job-list.h					\
	pk-notify.c					\
	pk-notify.h					\
	$(NULL)

pk_update_icon_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBNOTIFY_LIBS)				\
	$(GTK_LIBS)					\
	$(NULL)

pk_application_SOURCES =				\
	pk-application-main.c				\
	pk-debug.c					\
	pk-debug.h					\
	pk-application.c				\
	pk-application.h				\
	pk-marshal.c					\
	pk-marshal.h					\
	pk-task-utils.h					\
	pk-task-utils.c					\
	pk-task-common.c				\
	pk-task-common.h				\
	pk-task-client.c				\
	pk-task-client.h				\
	$(NULL)

pk_application_LDADD =					\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(LIBGLADE_LIBS)				\
	$(GTK_LIBS)					\
	$(NULL)

BUILT_SOURCES = 					\
	pk-marshal.c					\
	pk-marshal.h					\
	$(NULL)

pk-marshal.c: pk-marshal.list
	echo "#include \"pk-marshal.h\"" > $@ && \
	glib-genmarshal $< --prefix=pk_marshal --body >> $@

pk-marshal.h: pk-marshal.list
	glib-genmarshal $< --prefix=pk_marshal --header > $@

clean-local:
	rm -f *~
	rm -f pk-marshal.c pk-marshal.h

CLEANFILES = $(BUILT_SOURCES)

DISTCLEANFILES =					\
	$(NULL)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

