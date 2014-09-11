SUBDIRS =

if PK_BUILD_BROWSER_PLUGIN
SUBDIRS += browser-plugin
endif

if PK_BUILD_GSTREAMER_PLUGIN
SUBDIRS += gstreamer-plugin
endif

if PK_BUILD_GTK_MODULE
SUBDIRS += gtk-module
endif

if PK_BUILD_COMMAND_NOT_FOUND
SUBDIRS += command-not-found
endif

if PK_BUILD_CRON
SUBDIRS += cron
endif

if HAVE_BASH_COMPLETION
bashcompletiondir = $(BASH_COMPLETIONS_DIR)
dist_bashcompletion_DATA = pkcon
endif

EXTRA_DIST = generate-md-archive.sh

CLEANFILES = cached-metadata.tar

-include $(top_srcdir)/git.mk
