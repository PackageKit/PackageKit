## We require new-style dependency handling.
AUTOMAKE_OPTIONS = 1.7

INCLUDES =						\
	$(GLIB_CFLAGS)					\
	$(SQLITE_CFLAGS)				\
	-DPACKAGE_LOCALE_DIR=\"$(localedir)\"		\
	-DSYSCONFDIR=\""$(sysconfdir)"\" 		\
	-DDATADIR=\""$(datadir)"\" 		\
	-DEGG_LOG_FILE=\""$(PK_LOG_DIR)/PackageKit"\"	\
	-DEGG_VERBOSE="\"PK_VERBOSE\""			\
	-DEGG_LOGGING="\"PK_LOGGING\""			\
	-DEGG_CONSOLE="\"PK_CONSOLE\""			\
	-I$(top_srcdir)/lib

sbin_PROGRAMS = pk-app-install

pk_app_install_SOURCES =				\
	egg-debug.c					\
	egg-debug.h					\
	pk-app-install.c				\
	$(NULL)

pk_app_install_LDADD =					\
	$(GLIB_LIBS)					\
	$(SQLITE_LIBS)					\
	$(NULL)

pk_app_install_CFLAGS =					\
	$(WARNINGFLAGS_C)				\
	$(NULL)

