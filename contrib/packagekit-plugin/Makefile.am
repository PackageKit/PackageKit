plugindir=$(libdir)/mozilla/plugins
plugin_LTLIBRARIES = packagekit-plugin.la

packagekit_plugin_la_LDFLAGS = -rpath $(plugindir) -module -avoid-version -no-undefined
packagekit_plugin_la_CPPFLAGS =			\
	$(PK_BROWSER_PLUGIN_CFLAGS)		\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"	\
	-I $(top_srcdir)/libpackagekit		\
	-I $(srcdir)/sdk

packagekit_plugin_la_LIBADD =				\
	$(PK_BROWSER_PLUGIN_LIBS)			\
	$(top_builddir)/libpackagekit/libpackagekit.la

packagekit_plugin_la_SOURCES = 	\
	sdk/np_entry.cpp	\
	sdk/npn_gate.cpp	\
	sdk/npp_gate.cpp	\
	sdk/npplat.h		\
	sdk/pluginbase.h	\
				\
	src/contents.cpp	\
	src/contents.h		\
	src/plugin.cpp		\
	src/plugin.h

EXTRA_DIST =					\
	tests/test.html
