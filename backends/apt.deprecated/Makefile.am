NULL =

SUBDIRS = helpers
plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_apt.la

libpk_backend_apt_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_apt_la_LDFLAGS = -module -avoid-version $(APT_LIBS)
libpk_backend_apt_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(APT_CFLAGS)
libpk_backend_apt_la_CXXFLAGS = $(PK_PLUGIN_CFLAGS) $(APT_CFLAGS) -DPK_DB_DIR=\""$(PK_DB_DIR)"\"

libpk_backend_apt_la_SOURCES =				\
	pk-backend-apt.c				\
	pk-apt-search.h					\
	$(NULL)

if APT_SEARCH_PLAIN
libpk_backend_apt_la_SOURCES +=				\
	pk-apt-search-plain.c				\
	$(NULL)
endif

if APT_SEARCH_SQLITE
libpk_backend_apt_la_SOURCES +=				\
	pk-sqlite-pkg-cache.h				\
	pk-sqlite-pkg-cache.cpp				\
	pk-apt-build-db.cpp				\
	pk-apt-search-sqlite.cpp			\
	$(NULL)
endif
