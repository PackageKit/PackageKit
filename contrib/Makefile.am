SUBDIRS =

if BACKEND_TYPE_YUM
SUBDIRS += yum-packagekit
endif

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

if PK_BUILD_DEBUGINFO_INSTALL
SUBDIRS += debuginfo-install
endif

if PK_BUILD_PM_UTILS
SUBDIRS += pm-utils
endif

if PK_BUILD_DEVICE_REBIND
SUBDIRS += device-rebind
endif

bashcompletiondir = ${SYSCONFDIR}/bash_completion.d
dist_bashcompletion_DATA = pk-completion.bash

script_SCRIPTS =					\
	packagekit-bugreport.sh

EXTRA_DIST=$(script_SCRIPTS)

scriptdir = $(bindir)


-include $(top_srcdir)/git.mk
