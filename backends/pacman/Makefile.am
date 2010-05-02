PACMAN_CONFIG_FILE = $(confdir)/pacman.conf
PACMAN_DEFAULT_PATH = "/bin:/usr/bin:/sbin:/usr/sbin"

DEFS = -DPACMAN_CONFIG_FILE=\"$(PACMAN_CONFIG_FILE)\" \
       -DPACMAN_DEFAULT_PATH=\"$(PACMAN_DEFAULT_PATH)\"

confdir = $(PK_CONF_DIR)/pacman.d
conf_DATA = pacman.conf

plugindir = $(PK_PLUGIN_DIR)
plugin_LTLIBRARIES = libpk_backend_pacman.la

libpk_backend_pacman_la_SOURCES = backend-error.c \
                                  backend-pacman.c
libpk_backend_pacman_la_LIBADD = $(PK_PLUGIN_LIBS) \
                                 $(PACMAN_LIBS)
libpk_backend_pacman_la_LDFLAGS = -module -avoid-version
libpk_backend_pacman_la_CFLAGS = $(PK_PLUGIN_CFLAGS) \
                                 $(PACMAN_CFLAGS) \
                                 $(WARNINGFLAGS_C)

EXTRA_DIST = $(conf_DATA) $(libpk_backend_pacman_la_SOURCES:.c=.h)
