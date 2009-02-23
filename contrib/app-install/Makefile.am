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

noinst_PROGRAMS = pk-app-install-add pk-app-install-generate

sbin_PROGRAMS = pk-app-install-create pk-app-install-remove

pk_app_install_create_SOURCES =				\
	egg-debug.c					\
	egg-debug.h					\
	pk-app-install-create.c				\
	pk-app-install-common.h				\
	$(NULL)
pk_app_install_create_LDADD = $(GLIB_LIBS) $(SQLITE_LIBS)
pk_app_install_create_CFLAGS = $(WARNINGFLAGS_C)

pk_app_install_remove_SOURCES =				\
	egg-debug.c					\
	egg-debug.h					\
	pk-app-install-remove.c				\
	pk-app-install-common.h				\
	$(NULL)
pk_app_install_remove_LDADD = $(GLIB_LIBS) $(SQLITE_LIBS)
pk_app_install_remove_CFLAGS = $(WARNINGFLAGS_C)

pk_app_install_add_SOURCES =				\
	egg-debug.c					\
	egg-debug.h					\
	pk-app-install-add.c				\
	pk-app-install-common.h				\
	$(NULL)
pk_app_install_add_LDADD = $(GLIB_LIBS) $(SQLITE_LIBS)
pk_app_install_add_CFLAGS = $(WARNINGFLAGS_C)

pk_app_install_generate_SOURCES =			\
	egg-debug.c					\
	egg-debug.h					\
	pk-app-install-generate.c			\
	pk-app-install-common.h				\
	$(NULL)
pk_app_install_generate_LDADD = $(GLIB_LIBS) $(SQLITE_LIBS)
pk_app_install_generate_CFLAGS = $(WARNINGFLAGS_C)

