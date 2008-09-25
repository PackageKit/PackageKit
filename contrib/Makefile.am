SUBDIRS = udev

if BACKEND_TYPE_YUM
SUBDIRS += yum-packagekit
endif

if PK_BUILD_BROWSER_PLUGIN
SUBDIRS += packagekit-plugin
endif

if PK_BUILD_GSTREAMER_PLUGIN
SUBDIRS += gstreamer-plugin
endif

bashcompletiondir = ${SYSCONFDIR}/bash_completion.d
dist_bashcompletion_DATA = pk-completion.bash

script_SCRIPTS =					\
	packagekit-bugreport.sh

EXTRA_DIST=$(script_SCRIPTS)

scriptdir = $(bindir)

