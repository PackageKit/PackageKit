SUBDIRS=po .

plugindir=$(libdir)/mozilla/plugins
plugin_LTLIBRARIES = packagekit-plugin.la

packagekit_plugin_la_LDFLAGS = -rpath $(plugindir) -module -avoid-version -no-undefined
packagekit_plugin_la_CPPFLAGS =			\
	$(PLUGIN_CFLAGS)			\
	-DLOCALEDIR=\"$(localedir)\"		\
	-I $(top_srcdir)/sdk

packagekit_plugin_la_LIBADD = $(PLUGIN_LIBS)

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
	src/plugin.h		\
	src/util.cpp		\
	src/util.h

DISTCLEANFILES =				\
	intltool-extract			\
	intltool-merge				\
	intltool-update

EXTRA_DIST =					\
	intltool-extract.in 			\
	intltool-merge.in 			\
	intltool-update.in			\
	test.html
