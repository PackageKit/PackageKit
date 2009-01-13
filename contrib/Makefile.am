SUBDIRS =

if BACKEND_TYPE_YUM
SUBDIRS += yum-packagekit
endif

if PK_BUILD_RUCK
SUBDIRS += ruck
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

bashcompletiondir = ${SYSCONFDIR}/bash_completion.d
dist_bashcompletion_DATA = pk-completion.bash

script_SCRIPTS =					\
	packagekit-bugreport.sh

EXTRA_DIST=$(script_SCRIPTS)

scriptdir = $(bindir)

