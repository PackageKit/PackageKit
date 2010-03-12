helperdir = $(datadir)/PackageKit/helpers/yum
dist_helper_DATA = 			\
	yum-comps-groups.conf		\
	yumBackend.py			\
	yumComps.py			\
	yumMediaManager.py		\
	yumFilter.py

INCLUDES = \
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
	-DZIF_COMPILATION					\
	$(SQLITE_CFLAGS)					\
	$(RPM_CFLAGS)						\
	$(ZLIB_CFLAGS)						\
	$(BZLIB_CFLAGS)						\
	$(SOUP_CFLAGS)						\
	$(ARCHIVE_CFLAGS)					\
	-DSYSCONFDIR=\""$(sysconfdir)"\"			\
	-I$(top_srcdir)/backends/yum/libzif			\
	-I$(top_builddir)/backends/yum/libzif

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_yum.la
libpk_backend_yum_la_SOURCES = pk-backend-yum.c
libpk_backend_yum_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_yum_la_LDFLAGS = -module -avoid-version
libpk_backend_yum_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libpk_backend_yum_la_SOURCES += 				\
	libzif/egg-debug.c					\
	libzif/egg-debug.h					\
	libzif/egg-string.c					\
	libzif/egg-string.h					\
	libzif/zif.h						\
	libzif/zif-lock.c					\
	libzif/zif-lock.h					\
	libzif/zif-completion.c					\
	libzif/zif-completion.h					\
	libzif/zif-string.c					\
	libzif/zif-string.h					\
	libzif/zif-depend.c					\
	libzif/zif-depend.h					\
	libzif/zif-package.c					\
	libzif/zif-package.h					\
	libzif/zif-package-local.c				\
	libzif/zif-package-local.h				\
	libzif/zif-package-remote.c				\
	libzif/zif-package-remote.h				\
	libzif/zif-store.c					\
	libzif/zif-store.h					\
	libzif/zif-store-local.c				\
	libzif/zif-store-local.h				\
	libzif/zif-store-remote.c				\
	libzif/zif-store-remote.h				\
	libzif/zif-repo-md.c					\
	libzif/zif-repo-md.h					\
	libzif/zif-repo-md-filelists.c				\
	libzif/zif-repo-md-filelists.h				\
	libzif/zif-repo-md-primary.c				\
	libzif/zif-repo-md-primary.h				\
	libzif/zif-repo-md-metalink.c				\
	libzif/zif-repo-md-metalink.h				\
	libzif/zif-repo-md-mirrorlist.c				\
	libzif/zif-repo-md-mirrorlist.h				\
	libzif/zif-repo-md-comps.c				\
	libzif/zif-repo-md-comps.h				\
	libzif/zif-store-array.c				\
	libzif/zif-store-array.h				\
	libzif/zif-repos.c					\
	libzif/zif-repos.h					\
	libzif/zif-utils.c					\
	libzif/zif-utils.h					\
	libzif/zif-groups.c					\
	libzif/zif-groups.h					\
	libzif/zif-config.c					\
	libzif/zif-config.h					\
	libzif/zif-monitor.c					\
	libzif/zif-monitor.h					\
	libzif/zif-download.c					\
	libzif/zif-download.h

libpk_backend_yum_la_LIBADD +=					\
	$(SQLITE_LIBS)						\
	$(ZLIB_LIBS)						\
	$(BZLIB_LIBS)						\
	$(RPM_LIBS)						\
	$(SOUP_LIBS)						\
	$(ARCHIVE_LIBS)						\
	$(NULL)

confdir = $(PK_CONF_DIR)
conf_DATA = Yum.conf

EXTRA_DIST =							\
	$(conf_DATA)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local:
	rm -f *.gcno
	rm -f *~

