SUBDIRS = helpers
plugindir = @PK_PLUGIN_DIR@
plugin_LTLIBRARIES = libpk_backend_apt.la
libpk_backend_apt_la_INCLUDES = $(APT_CFLAGS) $(SQLITE_CFLAGS)
libpk_backend_apt_la_SOURCES = pk-backend-apt.cpp sqlite-pkg-cache.c apt-build-db.cpp python-backend-common.c
libpk_backend_apt_la_LIBADD = @PK_PLUGIN_LIBS@ $(APT_LIBS) $(SQLITE_LIBS)
libpk_backend_apt_la_LDFLAGS = -module -avoid-version
libpk_backend_apt_la_CFLAGS = @PK_PLUGIN_CFLAGS@ -DDATABASEDIR=\""$(PK_DB_DIR)"\"
libpk_backend_apt_la_CXXFLAGS = @PK_PLUGIN_CFLAGS@ -DDATABASEDIR=\""$(PK_DB_DIR)"\"

