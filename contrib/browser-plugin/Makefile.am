plugindir=$(MOZILLA_PLUGIN_DIR)
plugin_LTLIBRARIES = packagekit-plugin.la

packagekit_plugin_la_LDFLAGS = -rpath $(plugindir) -module -avoid-version -no-undefined
packagekit_plugin_la_CPPFLAGS =			\
	$(PK_BROWSER_PLUGIN_CFLAGS)		\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"	\
	-DXP_UNIX				\
	-I$(srcdir)/sdk				\
	-I$(top_srcdir)/lib

packagekit_plugin_la_LIBADD =			\
	$(PK_BROWSER_PLUGIN_LIBS)		\
	$(top_builddir)/lib/packagekit-glib/libpackagekit-glib.la

packagekit_plugin_la_SOURCES =			\
	sdk/npplat.h				\
	sdk/pluginbase.h			\
	src/contents.cpp			\
	src/contents.h				\
	src/plugin.cpp				\
	src/plugin.h

EXTRA_DIST =					\
	tests/test.html

