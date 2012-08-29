SUBDIRS = gtk2

if PK_BUILD_GTK_MODULE
APP = gedit --new-window GLASS.txt

moduledir = $(LIBDIR)/gtk-3.0/modules
module_LTLIBRARIES = libpk-gtk-module.la
libpk_gtk_module_la_SOURCES = pk-gtk-module.c
libpk_gtk_module_la_LIBADD = $(PK_GTK_MODULE_LIBS)
libpk_gtk_module_la_LDFLAGS = -module -avoid-version
libpk_gtk_module_la_CFLAGS = $(PK_GTK_MODULE_CFLAGS) $(WARNINGFLAGS_C)

gtkmoduledir = $(libdir)/gnome-settings-daemon-3.0/gtk-modules
dist_gtkmodule_DATA = pk-gtk-module.desktop

clean-local:
	rm -f *~

local: all
	GTK_MODULES="$$GTK_MODULES:$$PWD/.libs/libpk-gtk-module.so" $(APP)
endif

EXTRA_DIST =						\
	$(gsettings_SCHEMAS)				\
	README						\
	GLASS.txt


-include $(top_srcdir)/git.mk
