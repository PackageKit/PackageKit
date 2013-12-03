shared_SOURCES =							\
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
	hif-sack.c							\
	hif-sack.h							\
	hif-source.c							\
	hif-source.h							\
	hif-state.c							\
	hif-state.h							\
	hif-utils.c							\
	hif-utils.h

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_hawkey.la
libpk_backend_hawkey_la_SOURCES =					\
	$(shared_SOURCES)						\
	pk-backend-hawkey.c
libpk_backend_hawkey_la_LIBADD = $(PK_PLUGIN_LIBS) $(HAWKEY_LIBS)
libpk_backend_hawkey_la_CPPFLAGS =					\
	$(HAWKEY_CFLAGS)						\
	-DG_LOG_DOMAIN=\"PackageKit-Hawkey\"				\
	-DPIDFILE=\""/var/run/hif"\"
libpk_backend_hawkey_la_LDFLAGS = -module -avoid-version
libpk_backend_hawkey_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

PACKAGEKIT_LIBS =							\
	$(top_builddir)/lib/packagekit-glib2/libpackagekit-glib2.la	\
	$(top_builddir)/lib/packagekit-glib2/libpackagekitprivate.a

check_PROGRAMS =							\
	hif-self-test

hif_self_test_SOURCES =							\
	$(shared_SOURCES)						\
	hif-self-test.c

hif_self_test_LDADD =							\
	$(GLIB_LIBS)							\
	$(HAWKEY_LIBS)							\
	$(PACKAGEKIT_LIBS)						\
	$(PK_PLUGIN_LIBS)						\
	$(SQLITE_LIBS)

hif_self_test_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)
hif_self_test_CPPFLAGS =						\
	$(HAWKEY_CFLAGS)						\
	-DG_LOG_DOMAIN=\"PackageKit-Hawkey\"				\
	-DPIDFILE=\""/tmp/hif"\"

TESTS = hif-self-test

-include $(top_srcdir)/git.mk
