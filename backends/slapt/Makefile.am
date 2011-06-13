INCLUDES = \
	-DG_LOG_DOMAIN=\"PackageKit-Slapt\"

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_slapt.la
libpk_backend_slapt_la_SOURCES = pk-backend-slapt.c
libpk_backend_slapt_la_LIBADD = $(PK_PLUGIN_LIBS) $(SLAPT_LIBS)
libpk_backend_slapt_la_LDFLAGS = -module -avoid-version
libpk_backend_slapt_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)
libpk_backend_slapt_la_INCLUDES = $(SLAPT_CFLAGS)

-include $(top_srcdir)/git.mk
