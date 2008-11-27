NULL =
APP = gedit --new-window GLASS.txt

moduledir = $(LIBDIR)/gtk-2.0/modules
module_LTLIBRARIES = pk-gtk-module.la
pk_gtk_module_la_SOURCES = pk-gtk-module.c
pk_gtk_module_la_LIBADD = $(PK_GTK_MODULE_LIBS)
pk_gtk_module_la_LDFLAGS = -module -avoid-version
pk_gtk_module_la_CFLAGS = $(PK_GTK_MODULE_CFLAGS)

clean-local:
	rm -f *~

local: all
	GTK_MODULES="$$GTK_MODULES:$$PWD/.libs/pk-gtk-module.so" $(APP)

EXTRA_DIST =						\
	README						\
	GLASS.txt					\
	$(NULL)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

