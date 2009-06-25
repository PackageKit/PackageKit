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

libexec_PROGRAMS = pk-debuginfo-install

pk_debuginfo_install_SOURCES =				\
	egg-debug.c					\
	egg-debug.h					\
	pk-debuginfo-install.c				\
	$(NULL)

pk_debuginfo_install_LDADD =				\
	$(GLIB_LIBS)					\
	$(DBUS_LIBS)					\
	$(PK_GLIB_LIBS)					\
	$(NULL)

pk_debuginfo_install_CFLAGS =				\
	$(WARNINGFLAGS_C)				\
	$(NULL)

