if PK_BUILD_GTK_MODULE
moduledir = $(LIBDIR)/gtk-2.0/modules
module_LTLIBRARIES = libpk-gtk-module.la
libpk_gtk_module_la_SOURCES = pk-gtk-module-gtk2.c
libpk_gtk_module_la_LIBADD = $(PK_GTK_MODULE_GTK2_LIBS)
libpk_gtk_module_la_LDFLAGS = -module -avoid-version
libpk_gtk_module_la_CFLAGS = $(PK_GTK_MODULE_GTK2_CFLAGS) $(WARNINGFLAGS_C)
endif

-include $(top_srcdir)/git.mk
