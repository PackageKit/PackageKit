NULL =
APP = gedit --new-window GLASS.txt

moduledir = $(LIBDIR)/gtk-2.0/modules
module_LTLIBRARIES = libpk-gtk-module.la
libpk_gtk_module_la_SOURCES = pk-gtk-module.c
libpk_gtk_module_la_LIBADD = $(PK_GTK_MODULE_LIBS)
libpk_gtk_module_la_LDFLAGS = -module -avoid-version
libpk_gtk_module_la_CFLAGS = $(PK_GTK_MODULE_CFLAGS) $(WARNINGFLAGS_C)

clean-local:
	rm -f *~

local: all
	GTK_MODULES="$$GTK_MODULES:$$PWD/.libs/libpk-gtk-module.so" $(APP)

EXTRA_DIST =						\
	README						\
	GLASS.txt					\
	$(NULL)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

