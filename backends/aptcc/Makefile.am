INCLUDES = \
	-DDATADIR=\"$(datadir)\"			\
	-DG_LOG_DOMAIN=\"PackageKit-Aptcc\"

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_aptcc.la
libpk_backend_aptcc_la_SOURCES = pkg_acqfile.cpp \
				 aptcc_show_broken.cpp \
				 aptcc_show_error.cpp \
				 apt-utils.cpp \
				 acqprogress.cpp \
				 deb-file.cpp \
				 matcher.cpp \
				 gstMatcher.cpp \
				 rsources.cpp \
				 apt.cpp \
				 pk-backend-aptcc.cpp
libpk_backend_aptcc_la_LIBADD = -lcrypt -lapt-pkg -lapt-inst $(PK_PLUGIN_LIBS)
libpk_backend_aptcc_la_LDFLAGS = -module -avoid-version $(APTCC_LIBS) $(GSTREAMER_LIBS)
libpk_backend_aptcc_la_CFLAGS = $(PK_PLUGIN_CFLAGS)
libpk_backend_aptcc_la_CPPFLAGS = $(PK_PLUGIN_CFLAGS) $(APTCC_CFLAGS) $(GSTREAMER_CFLAGS)

aptconfdir = ${SYSCONFDIR}/apt/apt.conf.d
aptconf_DATA = 20packagekit

EXTRA_DIST = 20packagekit \
	     apt.h \
	     apt-utils.h \
	     gstMatcher.h \
	     matcher.h \
	     deb-file.h \
	     aptcc_show_broken.h \
	     acqprogress.h \
	     aptcc_show_error.h \
	     pkg_acqfile.h \
	     rsources.h

helperdir = $(datadir)/PackageKit/helpers/aptcc
dist_helper_DATA =					\
	get-distro-upgrade.py				\
	pkconffile					\
	pkconffile.nodiff				\
	$(NULL)

install-data-hook:
	chmod +x $(DESTDIR)$(helperdir)/*.py $(DESTDIR)$(helperdir)/pkconffile

clean-local :
	rm -f *~
	rm -f *.pyc

-include $(top_srcdir)/git.mk
