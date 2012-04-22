INCLUDES = \
	-DDATADIR=\"$(datadir)\"		\
	-DG_LOG_DOMAIN=\"PackageKit-Aptcc\"

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_aptcc.la
libpk_backend_aptcc_la_SOURCES = pkg_acqfile.cpp \
				 acqpkitstatus.cpp \
				 deb-file.cpp \
				 matcher.cpp \
				 gstMatcher.cpp \
				 apt-messages.cpp \
				 apt-utils.cpp \
				 apt-sourceslist.cpp \
				 OpPackageKitProgress.cpp \
                                 AptCacheFile.cpp \
				 apt-intf.cpp \
				 pk-backend-aptcc.cpp
libpk_backend_aptcc_la_LIBADD = -lcrypt -lapt-pkg -lapt-inst $(PK_PLUGIN_LIBS)
libpk_backend_aptcc_la_LDFLAGS = -module -avoid-version $(APTCC_LIBS) $(GSTREAMER_LIBS)
libpk_backend_aptcc_la_CFLAGS = $(PK_PLUGIN_CFLAGS)
libpk_backend_aptcc_la_CPPFLAGS = $(PK_PLUGIN_CFLAGS) $(APTCC_CFLAGS) $(GSTREAMER_CFLAGS)

aptconfdir = ${SYSCONFDIR}/apt/apt.conf.d
aptconf_DATA = 20packagekit

EXTRA_DIST = 20packagekit \
	     apt-intf.h \
	     apt-utils.h \
	     apt-sourceslist.h \
	     gstMatcher.h \
	     matcher.h \
	     deb-file.h \
	     apt-messages.h \
	     acqpkitstatus.h \
	     OpPackageKitProgress.h \
             AptCacheFile.h \
	     pkg_acqfile.h

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
