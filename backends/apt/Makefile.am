plugindir = @PK_PLUGIN_DIR@
plugin_LTLIBRARIES = libpk_backend_apt.la
libpk_backend_apt_la_INCLUDES = $(APT_CFLAGS)
libpk_backend_apt_la_SOURCES = pk-backend-apt.cpp
libpk_backend_apt_la_LIBADD = @PK_PLUGIN_LIBS@ $(APT_LIBS)
libpk_backend_apt_la_LDFLAGS = -module -avoid-version
libpk_backend_apt_la_CXXFLAGS = @PK_PLUGIN_CFLAGS@ -DDATABASEDIR=\""$(PK_DB_DIR)"\"

