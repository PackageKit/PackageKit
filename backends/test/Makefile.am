INCLUDES = \
	-DG_LOG_DOMAIN=\"PackageKit-Test\"

SUBDIRS = helpers
plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES =			\
	libpk_backend_test_nop.la	\
	libpk_backend_test_fail.la	\
	libpk_backend_test_spawn.la	\
	libpk_backend_test_succeed.la	\
	libpk_backend_test_thread.la

libpk_backend_test_nop_la_SOURCES = pk-backend-test-nop.c
libpk_backend_test_nop_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_test_nop_la_LDFLAGS = -module -avoid-version
libpk_backend_test_nop_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libpk_backend_test_fail_la_SOURCES = pk-backend-test-fail.c
libpk_backend_test_fail_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_test_fail_la_LDFLAGS = -module -avoid-version
libpk_backend_test_fail_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libpk_backend_test_spawn_la_SOURCES = pk-backend-test-spawn.c
libpk_backend_test_spawn_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_test_spawn_la_LDFLAGS = -module -avoid-version
libpk_backend_test_spawn_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libpk_backend_test_succeed_la_SOURCES = pk-backend-test-succeed.c
libpk_backend_test_succeed_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_test_succeed_la_LDFLAGS = -module -avoid-version
libpk_backend_test_succeed_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)

libpk_backend_test_thread_la_SOURCES = pk-backend-test-thread.c
libpk_backend_test_thread_la_LIBADD = $(PK_PLUGIN_LIBS)
libpk_backend_test_thread_la_LDFLAGS = -module -avoid-version
libpk_backend_test_thread_la_CFLAGS = $(PK_PLUGIN_CFLAGS) $(WARNINGFLAGS_C)


-include $(top_srcdir)/git.mk
