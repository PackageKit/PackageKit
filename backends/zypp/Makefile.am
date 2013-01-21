AM_CPPFLAGS = \
	-DG_LOG_DOMAIN=\"PackageKit-Zypp\"

#SUBDIRS = helpers
plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_zypp.la
libpk_backend_zypp_la_SOURCES =	pk-backend-zypp.cpp		
libpk_backend_zypp_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_zypp_la_LDFLAGS = -module -avoid-version $(ZYPP_LIBS)
libpk_backend_zypp_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_CPP)
libpk_backend_zypp_la_CXXFLAGS = $(PK_PLUGIN_CXXFLAGS) --std=c++0x -Wall -Woverloaded-virtual -Wnon-virtual-dtor
libpk_backend_zypp_la_CPPFLAGS = $(PK_PLUGIN_CFLAGS) $(ZYPP_CFLAGS) -Wno-deprecated

EXTRA_DIST =		\
	zypp-utils.h	\
	zypp-events.h \
	pk-backend-zypp-private.h


-include $(top_srcdir)/git.mk
