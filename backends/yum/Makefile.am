helperdir = $(datadir)/PackageKit/helpers/yum
dist_helper_DATA = 			\
	yum-comps-groups.conf		\
	licenses.txt			\
	yumBackend.py			\
	yumComps.py			\
	yumFilter.py

AM_CPPFLAGS = \
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
	-DG_LOG_DOMAIN=\"PackageKit-Yum\"			\
	-DPK_COMPILATION					\
	-DSYSCONFDIR=\""$(sysconfdir)"\"

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_yum.la
libpk_backend_yum_la_SOURCES = pk-backend-yum.c
libpk_backend_yum_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_yum_la_LDFLAGS = -module -avoid-version
libpk_backend_yum_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

confdir = $(sysconfdir)/PackageKit
conf_DATA = Yum.conf

EXTRA_DIST =							\
	$(conf_DATA)

install-data-hook:
	chmod a+rx $(DESTDIR)$(helperdir)/*.py

clean-local:
	rm -f *.gcno
	rm -f *~


-include $(top_srcdir)/git.mk
