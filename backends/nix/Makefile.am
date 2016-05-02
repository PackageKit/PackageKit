plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_nix.la

libpk_backend_nix_la_SOURCES = \
  pk-backend-nix.cc \
  nix-helpers.cc \
  nix-lib-plus.cc
libpk_backend_nix_la_LIBADD = -lnixmain $(PK_PLUGIN_LIBS) $(NIX_LIBS)
libpk_backend_nix_la_LDFLAGS = -module -avoid-version
libpk_backend_nix_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(AM_CPPFLAGS)
libpk_backend_nix_la_CPPFLAGS = $(PK_PLUGIN_CFLAGS) $(NIX_CFLAGS) $(AM_CPPFLAGS)

-include $(top_srcdir)/git.mk
