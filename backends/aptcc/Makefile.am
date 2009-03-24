plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_aptcc.la
libpk_backend_aptcc_la_SOURCES = pk-backend-aptcc.cpp \
                                 apt.cpp \
                                 apt-utils.cpp \
                                 matcher.cpp \
				 aptcc_show_broken.cpp
libpk_backend_aptcc_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_aptcc_la_LDFLAGS = -module -avoid-version $(APTCC_LIBS)
libpk_backend_aptcc_la_CFLAGS = $(PK_PLUGIN_CFLAGS)
libpk_backend_aptcc_la_CPPFLAGS = $(PK_PLUGIN_CFLAGS) $(APTCC_CFLAGS)
EXTRA_DIST = apt.h \
             apt-utils.h \
             matcher.h \
	     aptcc_show_broken.h