AM_CPPFLAGS = \
	$(HAWKEY_CFLAGS)						\
	-DSYSCONFDIR=\""$(sysconfdir)"\"				\
	-DLOCALSTATEDIR=\""$(localstatedir)"\"				\
	-DG_LOG_DOMAIN=\"PackageKit-Hawkey\"

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_hawkey.la
libpk_backend_hawkey_la_SOURCES =					\
	pk-backend-hawkey.c						\
	hif-config.c							\
	hif-config.h							\
	hif-db.c							\
	hif-db.h							\
	hif-goal.c							\
	hif-goal.h							\
	hif-keyring.c							\
	hif-keyring.h							\
	hif-lock.c							\
	hif-lock.h							\
	hif-package.c							\
	hif-package.h							\
	hif-rpmts.c							\
	hif-rpmts.h							\
	hif-source.c							\
	hif-source.h							\
	hif-state.c							\
	hif-state.h							\
	hif-utils.c							\
	hif-utils.h
libpk_backend_hawkey_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_hawkey_la_LDFLAGS = -module -avoid-version
libpk_backend_hawkey_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)
libpk_backend_hawkey_la_LIBADD += $(HAWKEY_LIBS)

-include $(top_srcdir)/git.mk
