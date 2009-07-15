plugindir=$(MOZILLA_PLUGIN_DIR)
plugin_LTLIBRARIES = packagekit-plugin.la

packagekit_plugin_la_LDFLAGS = -rpath $(plugindir) -module -avoid-version -no-undefined
packagekit_plugin_la_CPPFLAGS =			\
	$(PK_BROWSER_PLUGIN_CFLAGS)		\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"	\
	-DMOZ_X11				\
	-I$(top_srcdir)/lib

packagekit_plugin_la_CFLAGS =			\
	$(WARNINGFLAGS_C)			\
	$(NULL)

packagekit_plugin_la_LIBADD =			\
	$(PK_BROWSER_PLUGIN_LIBS)		\
	$(top_builddir)/lib/packagekit-glib/libpackagekit-glib.la

packagekit_plugin_la_SOURCES =			\
	pk-store.c				\
	pk-store.h				\
	pk-plugin.c				\
	pk-plugin.h				\
	pk-plugin-install.c			\
	pk-plugin-install.h			\
	pk-main.c				\
	pk-main.h

EXTRA_DIST =					\
	tests/test.html

