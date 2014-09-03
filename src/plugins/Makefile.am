## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

PK_GLIB2_LIBS =						\
	$(top_builddir)/lib/packagekit-glib2/libpackagekit-glib2.la

AM_CPPFLAGS =						\
	$(GIO_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	-DBINDIR=\"$(bindir)\"				\
	-DDATADIR=\"$(datadir)\"			\
	-DG_LOG_DOMAIN=\"PkPlugin\"			\
	-DLIBDIR=\""$(libdir)"\"			\
	-DLOCALSTATEDIR=\""$(localstatedir)"\"		\
	-DPK_COMPILATION				\
	-DSBINDIR=\"$(sbindir)\"			\
	-DSYSCONFDIR=\""$(sysconfdir)"\"		\
	-DTESTDATADIR=\""$(top_srcdir)/data/tests"\"	\
	-I$(top_srcdir)/lib

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = 					\
	packagekit-plugin.pc

clean-local:
	rm -f *~

# plugins
plugindir = $(LIBDIR)/packagekit-plugins-2
plugin_LTLIBRARIES =					\
	libpk_plugin_scripts.la				\
	libpk_plugin-systemd-updates.la

libpk_plugin_systemd_updates_la_SOURCES = pk-plugin-systemd-updates.c
libpk_plugin_systemd_updates_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_plugin_systemd_updates_la_LDFLAGS = -module -avoid-version
libpk_plugin_systemd_updates_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libpk_plugin_scripts_la_SOURCES = pk-plugin-scripts.c
libpk_plugin_scripts_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_plugin_scripts_la_LDFLAGS = -module -avoid-version
libpk_plugin_scripts_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

-include $(top_srcdir)/git.mk
