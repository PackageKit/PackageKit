plugindir=$(libdir)/mozilla/plugins
plugin_LTLIBRARIES = packagekit-plugin.la

packagekit_plugin_la_LDFLAGS = -rpath $(plugindir) -module -avoid-version -no-undefined
packagekit_plugin_la_CPPFLAGS =			\
	$(PK_BROWSER_PLUGIN_CFLAGS)		\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"	\
	-DXP_UNIX				\
	-I$(top_srcdir)/lib

if PK_XULRUNNER_PLUGIN_NEW
packagekit_plugin_la_CPPFLAGS += -I$(srcdir)/sdk-new
else
packagekit_plugin_la_CPPFLAGS += -I$(srcdir)/sdk
endif

packagekit_plugin_la_LIBADD =			\
	$(PK_BROWSER_PLUGIN_LIBS)		\
	$(top_builddir)/lib/packagekit-glib/libpackagekit-glib.la

packagekit_plugin_la_SOURCES =			\
	src/contents.cpp			\
	src/contents.h				\
	src/plugin.cpp				\
	src/plugin.h

if PK_XULRUNNER_PLUGIN_NEW
packagekit_plugin_la_SOURCES +=			\
	sdk-new/npplat.h			\
	sdk-new/pluginbase.h
else
packagekit_plugin_la_SOURCES +=			\
	sdk/np_entry.cpp			\
	sdk/npn_gate.cpp			\
	sdk/npp_gate.cpp			\
	sdk/npplat.h				\
	sdk/pluginbase.h
endif

EXTRA_DIST =					\
	tests/test.html

