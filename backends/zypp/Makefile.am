#SUBDIRS = helpers
plugindir = @PK_PLUGIN_DIR@
plugin_LTLIBRARIES = libpk_backend_zypp.la
libpk_backend_zypp_la_SOURCES = pk-backend-zypp.cpp
libpk_backend_zypp_la_LIBADD = @PK_PLUGIN_LIBS@
# FIXME: Do the pkg-config stuff in $top_srcdir/configure.ac
libpk_backend_zypp_la_LDFLAGS = -module -avoid-version `pkg-config --libs libzypp`
libpk_backend_zypp_la_CFLAGS = @PK_PLUGIN_CFLAGS@ `pkg-config --cflags libzypp`
libpk_backend_zypp_la_CPPFLAGS = @PK_PLUGIN_CFLAGS@ `pkg-config --cflags libzypp`

