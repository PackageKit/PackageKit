AM_CPPFLAGS = \
	-DG_LOG_DOMAIN=\"PackageKit-Urpmi\"

SUBDIRS = helpers
plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_urpmi.la
libpk_backend_urpmi_la_SOURCES = pk-backend-urpmi.c
libpk_backend_urpmi_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_urpmi_la_LDFLAGS = -module -avoid-version
libpk_backend_urpmi_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

clean-local:
	rm -f *.gcno


-include $(top_srcdir)/git.mk
