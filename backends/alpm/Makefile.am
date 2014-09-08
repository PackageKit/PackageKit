PK_BACKEND_CONFIG_FILE = $(confdir)/pacman.conf
PK_BACKEND_GROUP_FILE = $(confdir)/groups.list
PK_BACKEND_REPO_FILE = $(confdir)/repos.list

PK_BACKEND_DEFAULT_PATH = "/bin:/usr/bin:/sbin:/usr/sbin"

DEFS = -DPK_BACKEND_CONFIG_FILE=\"$(PK_BACKEND_CONFIG_FILE)\" \
       -DPK_BACKEND_GROUP_FILE=\"$(PK_BACKEND_GROUP_FILE)\" \
       -DPK_BACKEND_REPO_FILE=\"$(PK_BACKEND_REPO_FILE)\" \
       -DPK_BACKEND_DEFAULT_PATH=\"$(PK_BACKEND_DEFAULT_PATH)\" \
       -DPK_LOG_PREFIX=\"PACKAGEKIT\" \
       -DG_LOG_DOMAIN=\"PackageKit-alpm\"

confdir = $(sysconfdir)/PackageKit/alpm.d
conf_DATA =								\
	groups.list							\
	pacman.conf							\
	repos.list

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_alpm.la

libpk_backend_alpm_la_SOURCES =						\
	pk-backend-alpm.c						\
	pk-backend-alpm.h						\
	pk-alpm-config.c						\
	pk-alpm-config.h						\
	pk-alpm-databases.c						\
	pk-alpm-databases.h						\
	pk-alpm-depends.c						\
	pk-alpm-error.c							\
	pk-alpm-error.h							\
	pk-alpm-groups.c						\
	pk-alpm-groups.h						\
	pk-alpm-install.c						\
	pk-alpm-packages.c						\
	pk-alpm-packages.h						\
	pk-alpm-remove.c						\
	pk-alpm-search.c						\
	pk-alpm-sync.c							\
	pk-alpm-transaction.c						\
	pk-alpm-transaction.h						\
	pk-alpm-update.c
libpk_backend_alpm_la_LIBADD = $(PK_PLUGIN_LIBS) $(ALPM_LIBS)
libpk_backend_alpm_la_LDFLAGS = -module -avoid-version
libpk_backend_alpm_la_CFLAGS =						\
	$(PK_PLUGIN_CFLAGS)						\
	$(ALPM_CFLAGS)							\
	$(WARNINGFLAGS_C)

EXTRA_DIST = $(conf_DATA)

-include $(top_srcdir)/git.mk
