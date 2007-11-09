SUBDIRS = helpers
plugindir = @PK_PLUGIN_DIR@
plugin_LTLIBRARIES = libpk_backend_apt.la
libpk_backend_apt_la_SOURCES = pk-backend-apt.c
libpk_backend_apt_la_LIBADD = @PK_PLUGIN_LIBS@
libpk_backend_apt_la_LDFLAGS = -module -avoid-version
libpk_backend_apt_la_CFLAGS = @PK_PLUGIN_CFLAGS@

if APT_SEARCH_PLAIN
libpk_backend_apt_la_SOURCES += pk-apt-search-plain.c
endif
