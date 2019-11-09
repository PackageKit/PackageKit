AM_CPPFLAGS = \
	-DDATADIR=\"$(datadir)\"		\
	-DG_LOG_DOMAIN=\"PackageKit-APTcc\"

PK_GLIB2_LIBS =						\
	$(top_builddir)/lib/packagekit-glib2/libpackagekit-glib2.la	\
	$(top_builddir)/lib/packagekit-glib2/libpackagekitprivate.a


%: %.in Makefile
	$(AM_V_GEN)sed -e 's|\@libexecdir\@|$(libexecdir)|' \
	               -e 's|\@PACKAGEKIT_USER\@|$(PACKAGEKIT_USER)|' $< > $@.tmp && mv $@.tmp $@

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_aptcc.la
libpk_backend_aptcc_la_SOURCES = pkg-list.cpp \
				 acqpkitstatus.cpp \
				 gst-matcher.cpp \
				 apt-messages.cpp \
				 apt-utils.cpp \
				 apt-sourceslist.cpp \
				 apt-cache-file.cpp \
				 apt-intf.cpp \
				 deb-file.cpp \
				 pk-backend-aptcc.cpp
libpk_backend_aptcc_la_LIBADD = -lcrypt \
				-lapt-pkg \
				$(APTINST_LIBS) \
				-lutil \
				$(PK_PLUGIN_LIBS)
libpk_backend_aptcc_la_LDFLAGS = -module \
				-avoid-version \
				$(APTCC_LIBS) \
				$(APPSTREAM_LIBS) \
				$(GSTREAMER_LIBS)
libpk_backend_aptcc_la_CFLAGS = $(PK_PLUGIN_CFLAGS) \
				$(AM_CPPFLAGS) \
				$(APPSTREAM_CFLAGS)
libpk_backend_aptcc_la_CPPFLAGS = $(PK_PLUGIN_CFLAGS) \
				  $(APTCC_CFLAGS) \
				  $(APPSTREAM_CFLAGS) \
				  $(GSTREAMER_CFLAGS) \
				  $(AM_CPPFLAGS)

aptconfdir = ${SYSCONFDIR}/apt/apt.conf.d
aptconf_DATA = 20packagekit

if HAVE_SYSTEMD
libexec_PROGRAMS = pk-debconf-helper
pk_debconf_helper_SOURCES =						\
	pk-debconf-helper.c

pk_debconf_helper_LDADD =						\
	$(GLIB_LIBS)					\
	$(GIO_LIBS)					\
	$(SYSTEMD_LIBS)					\
	$(PK_GLIB2_LIBS)

pk_debconf_helper_LDFLAGS =						\
	$(PIE_LDFLAGS)

pk_debconf_helper_CFLAGS =						\
	$(WARNINGFLAGS_C)
pk_debconf_helper_CPPFLAGS =						\
	$(PIE_CFLAGS)					\
	$(GLIB_CFLAGS)					\
	$(GIO_CFLAGS)					\
	$(SYSTEMD_CFLAGS)				\
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
	-DPK_COMPILATION				\
	-DG_LOG_DOMAIN=\"PackageKit\"			\
	-I$(top_builddir)/lib				\
	-I$(top_srcdir)					\
	-I$(top_srcdir)/lib
systemdservice_in_files =				\
	pk-debconf-helper.service.in				\
	pk-debconf-helper.socket.in

systemduserservicedir   = $(systemduserunitdir)
systemduserservice_DATA = pk-debconf-helper.socket \
			  pk-debconf-helper.service
endif

EXTRA_DIST = 20packagekit \
	     pkg-list.h \
	     apt-intf.h \
	     apt-utils.h \
	     apt-sourceslist.h \
	     apt-messages.h \
	     apt-cache-file.h \
	     gst-matcher.h \
	     deb-file.h \
	     acqpkitstatus.h

helperdir = $(datadir)/PackageKit/helpers/aptcc
dist_helper_DATA =					\
	pkconffile					\
	pkconffile.nodiff				\
	$(NULL)

install-data-hook:
	chmod +x $(DESTDIR)$(helperdir)/pkconffile

clean-local :
	rm -f *~

-include $(top_srcdir)/git.mk
