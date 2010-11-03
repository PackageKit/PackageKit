INCLUDES = \
	-DG_LOG_DOMAIN=\"PackageKit-Alpm\"

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_alpm.la
libpk_backend_alpm_la_SOURCES = pk-backend-alpm.c
libpk_backend_alpm_la_LIBADD = $(PK_PLUGIN_LIBS) -lalpm
libpk_backend_alpm_la_LDFLAGS = -module -avoid-version
libpk_backend_alpm_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

