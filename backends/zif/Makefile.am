helperdir = $(datadir)/PackageKit/helpers/zif
dist_helper_DATA = 			\
	zif-comps-groups.conf		\
	licenses.txt

INCLUDES = \
	-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE	\
	-DG_LOG_DOMAIN=\"PackageKit-Zif\"			\
	-DPK_COMPILATION					\
	$(ZIF_CFLAGS)						\
	-DDATADIR=\"$(datadir)\"				\
	-DSYSCONFDIR=\""$(sysconfdir)"\"

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_zif.la
libpk_backend_zif_la_SOURCES = pk-backend-zif.c
libpk_backend_zif_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_zif_la_LDFLAGS = -module -avoid-version
libpk_backend_zif_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)
libpk_backend_zif_la_LIBADD += $(ZIF_LIBS)

EXTRA_DIST =							\
	$(conf_DATA)

clean-local:
	rm -f *~


-include $(top_srcdir)/git.mk
