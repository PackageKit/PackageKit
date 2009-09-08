## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"		\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DEGG_LOG_FILE=\""$(PK_LOG_DIR)/PackageKit"\"	\
	-DEGG_VERBOSE="\"PK_VERBOSE\""			\
	-DEGG_LOGGING="\"PK_LOGGING\""			\
	-DEGG_CONSOLE="\"PK_CONSOLE\""			\
	-I$(top_srcdir)/lib

PK_GLIB_LIBS = $(top_builddir)/lib/packagekit-glib/libpackagekit-glib.la

libexec_PROGRAMS = pk-command-not-found

pk_command_not_found_SOURCES =				\
	egg-debug.c					\
	egg-debug.h					\
	egg-string.c					\
	egg-string.h					\
	pk-tools-common.c				\
	pk-tools-common.h				\
	pk-command-not-found.c				\
	$(NULL)

pk_command_not_found_LDADD =				\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(PK_GLIB_LIBS)					\
	$(NULL)

pk_command_not_found_CFLAGS =				\
	$(WARNINGFLAGS_C)				\
	$(NULL)

# Only build this functionality if we have glib2, and don't yet install it
if PK_BUILD_GLIB2

PK_GLIB2_LIBS =						\
	$(top_builddir)/lib/packagekit-glib2/libpackagekit-glib2.la	\
	$(top_builddir)/lib/packagekit-glib2/libpackagekitprivate.a	\
	$(NULL)

noinst_PROGRAMS = pk-command-not-found-test

pk_command_not_found_test_SOURCES =			\
	pk-command-not-found-test.c			\
	$(NULL)

pk_command_not_found_test_LDADD =			\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(PK_GLIB2_LIBS)				\
	$(NULL)

pk_command_not_found_test_CFLAGS =			\
	$(WARNINGFLAGS_C)				\
	$(NULL)

endif


bashprofiledir = ${SYSCONFDIR}/profile.d
dist_bashprofile_DATA = PackageKit.sh

PackageKit.sh: PackageKit.sh.in Makefile
	sed -e s!\@LIBEXECDIR\@!$(libexecdir)! < $< > $@

confdir = $(PK_CONF_DIR)
dist_conf_DATA = CommandNotFound.conf

EXTRA_DIST = PackageKit.sh.in

CLEANFILES = PackageKit.sh
