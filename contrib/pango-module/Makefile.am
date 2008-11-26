APP = gedit --new-window GLASS.txt

moduledir = $(LIBDIR)/gtk-2.0/modules
module_LTLIBRARIES = pk-pango-module.la
pk_pango_module_la_SOURCES = pk-pango-module.c
pk_pango_module_la_LIBADD = $(PK_PANGO_MODULE_LIBS)
pk_pango_module_la_LDFLAGS = -module -avoid-version
pk_pango_module_la_CFLAGS = $(PK_PANGO_MODULE_CFLAGS)

clean-local:
	rm -f *~

local: all
	GTK_MODULES="$$GTK_MODULES:$$PWD/.libs/pk-pango-module.so" $(APP)

MAINTAINERCLEANFILES =					\
	*~			      			\
	Makefile.in					\
	$(NULL)

