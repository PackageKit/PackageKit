plugindir=$(MOZILLA_PLUGIN_DIR)
plugin_LTLIBRARIES = packagekit-plugin.la

packagekit_plugin_la_LDFLAGS = -rpath $(plugindir) -module -avoid-version -no-undefined
packagekit_plugin_la_CPPFLAGS =			\
	$(PK_BROWSER_PLUGIN_CFLAGS)		\
	-DG_LOG_DOMAIN=\"PK-BrowserPlugin\"	\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"	\
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
	-DMOZ_X11				\
	-DGSEAL_ENABLE				\
	-I$(top_srcdir)/lib

packagekit_plugin_la_CFLAGS =			\
	$(WARNINGFLAGS_C)			\
	$(NULL)

packagekit_plugin_la_LIBADD =			\
	$(PK_BROWSER_PLUGIN_LIBS)		\
	$(NULL)

packagekit_plugin_la_LIBADD +=			\
	$(top_builddir)/lib/packagekit-glib2/libpackagekit-glib2.la

packagekit_plugin_la_SOURCES =			\
	pk-plugin.c				\
	pk-plugin.h				\
	pk-plugin-install.c			\
	pk-plugin-install.h			\
	pk-main.c

EXTRA_DIST =					\
	tests/test.html


-include $(top_srcdir)/git.mk
