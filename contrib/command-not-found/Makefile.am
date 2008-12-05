## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(DBUS_CFLAGS)					\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"		\
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
	pk-command-not-found.c				\
	$(NULL)

pk_command_not_found_LDADD =				\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(PK_GLIB_LIBS)					\
	$(NULL)

bashprofiledir = ${SYSCONFDIR}/profile.d
dist_bashprofile_DATA = PackageKit.sh

