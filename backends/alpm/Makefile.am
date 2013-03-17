PK_BACKEND_CONFIG_FILE = $(confdir)/pacman.conf
PK_BACKEND_GROUP_FILE = $(confdir)/groups.list
PK_BACKEND_REPO_FILE = $(confdir)/repos.list

PK_BACKEND_DEFAULT_PATH = "/bin:/usr/bin:/sbin:/usr/sbin"
PK_BACKEND_DEFAULT_ROOT = "/"
PK_BACKEND_DEFAULT_DBPATH = $(localstatedir)/lib/pacman/
PK_BACKEND_DEFAULT_GPGDIR = $(sysconfdir)/pacman.d/gnupg/
PK_BACKEND_DEFAULT_CACHEDIR = $(localstatedir)/cache/pacman/pkg/
PK_BACKEND_DEFAULT_LOGFILE = $(localstatedir)/log/pacman.log

ALPM_CACHE_PATH = $(localstatedir)/lib/pacman/sync
ALPM_PACKAGE_URL = "http://www.archlinux.org/packages/%s/%s/%s/"

DEFS = -DPK_BACKEND_CONFIG_FILE=\"$(PK_BACKEND_CONFIG_FILE)\" \
       -DPK_BACKEND_GROUP_FILE=\"$(PK_BACKEND_GROUP_FILE)\" \
       -DPK_BACKEND_REPO_FILE=\"$(PK_BACKEND_REPO_FILE)\" \
       -DPK_BACKEND_DEFAULT_PATH=\"$(PK_BACKEND_DEFAULT_PATH)\" \
       -DPK_BACKEND_DEFAULT_ROOT=\"$(PK_BACKEND_DEFAULT_ROOT)\" \
       -DPK_BACKEND_DEFAULT_DBPATH=\"$(PK_BACKEND_DEFAULT_DBPATH)\" \
       -DPK_BACKEND_DEFAULT_GPGDIR=\"$(PK_BACKEND_DEFAULT_GPGDIR)\" \
       -DPK_BACKEND_DEFAULT_CACHEDIR=\"$(PK_BACKEND_DEFAULT_CACHEDIR)\" \
       -DPK_BACKEND_DEFAULT_LOGFILE=\"$(PK_BACKEND_DEFAULT_LOGFILE)\" \
       -DALPM_CACHE_PATH=\"$(ALPM_CACHE_PATH)\" \
       -DALPM_PACKAGE_URL=\"$(ALPM_PACKAGE_URL)\" \
       -DPK_LOG_PREFIX=\"PACKAGEKIT\" \
       -DG_LOG_DOMAIN=\"PackageKit-alpm\"

confdir = $(PK_CONF_DIR)/alpm.d
conf_DATA = groups.list \
	    pacman.conf \
	    repos.list

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_alpm.la

libpk_backend_alpm_la_SOURCES = pk-backend-alpm.c \
				pk-backend-config.c \
				pk-backend-databases.c \
				pk-backend-depends.c \
				pk-backend-error.c \
				pk-backend-groups.c \
				pk-backend-install.c \
				pk-backend-packages.c \
				pk-backend-remove.c \
				pk-backend-search.c \
				pk-backend-sync.c \
				pk-backend-transaction.c \
				pk-backend-update.c
libpk_backend_alpm_la_LIBADD = $(PK_PLUGIN_LIBS) \
			       $(ALPM_LIBS)
libpk_backend_alpm_la_LDFLAGS = -module \
				-avoid-version
libpk_backend_alpm_la_CFLAGS = $(PK_PLUGIN_CFLAGS) \
			       $(ALPM_CFLAGS) \
			       $(WARNINGFLAGS_C)

EXTRA_DIST = $(conf_DATA) $(libpk_backend_alpm_la_SOURCES:.c=.h)

-include $(top_srcdir)/git.mk
